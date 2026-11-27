// Copyright (c) 2026 Huawei Technologies Co.,Ltd.
// Licensed under Mulan PSL v2. See the repository license.
#include <sqlite3.h>

#include <algorithm>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_map>

#include "TraceWriters.h"
#include "db/DBConstant.h"
#include "db/DataBase.h"

namespace msmonitor::process
{
namespace
{
namespace schema = dynolog_npu::ipc_monitor::db;
using Json = nlohmann::json;
struct DbConstants final
{
    static constexpr std::uint64_t UnknownId{std::numeric_limits<std::uint32_t>::max()};
    static constexpr std::uint64_t MarkerConnectionOffset{4000000000ULL};
    static constexpr int BusyTimeoutMs{5000};
};

const std::vector<std::pair<std::uint16_t, std::string>> &CommunicationTypes()
{
    // Preserve the MSPTI enum IDs without requiring the SDK in an offline build.
    static const std::vector<std::pair<std::uint16_t, std::string>> types{
        {0, "INT8"},    {1, "INT16"}, {2, "INT32"},    {3, "FP16"},     {4, "FP32"},     {5, "INT64"},
        {6, "UINT64"},  {7, "UINT8"}, {8, "UINT16"},   {9, "UINT32"},   {10, "FP64"},    {11, "BFP16"},
        {12, "INT128"}, {14, "HIF8"}, {15, "FP8E4M3"}, {16, "FP8E5M2"}, {17, "FP8E8M0"}, {65535, "INVALID_TYPE"}};
    return types;
}

// One connection, transaction and prepared statement per table for the entire step.
// Schema columns are reused from ipc_monitor; collection/runtime managers are not linked.
class StepDatabase
{
   public:
    explicit StepDatabase(const std::filesystem::path &file)
    {
        sqlite3 *raw = nullptr;
        // The rank's trace worker exclusively owns this connection; never share it across threads.
        const auto status = sqlite3_open_v2(file.string().c_str(), &raw,
                                            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr);
        database_.reset(raw);
        Check(status);
        Check(sqlite3_busy_timeout(database_.get(), DbConstants::BusyTimeoutMs));
        // Publish the staging DB only after a successful close.
        // Avoid durable journal I/O for every step; crashes leave unpublished staging files.
        Execute("PRAGMA journal_mode=MEMORY");
        Execute("PRAGMA synchronous=OFF");
        Execute("PRAGMA cache_size=-8192");
        Execute("BEGIN IMMEDIATE");
        // Empty standard tables are still present for consumers with fixed schema queries.
        for (const auto &table :
             std::vector<std::string>{schema::TABLE_STRING_IDS, schema::TABLE_SESSION_TIME_INFO, schema::TABLE_CANN_API,
                                      schema::TABLE_TASK, schema::TABLE_COMPUTE_TASK_INFO,
                                      schema::TABLE_COMMUNICATION_OP, schema::TABLE_MSTX, schema::TABLE_MSTX_EVENT_TYPE,
                                      schema::TABLE_HCCL_DATA_TYPE, schema::TABLE_API_TYPE, schema::TABLE_HOST_INFO,
                                      schema::TABLE_NPU_INFO, schema::TABLE_RANK_DEVICE_MAP, schema::TABLE_META_DATA})
        {
            CreateTable(table, schema_.GetTableCols(table));
        }
    }

    void Execute(const std::string &sql)
    {
        Check(sqlite3_exec(database_.get(), sql.c_str(), nullptr, nullptr, nullptr));
    }

    template <typename... Values>
    void Insert(const std::string &table, const Values &...values)
    {
        auto *statement = Prepare(table, sizeof...(values));
        int index = 0;
        (BindValue(statement, ++index, values), ...);
        FinishRow(statement);
    }

    // Small metadata rows may remain JSON; activity rows bind native values directly.
    void InsertRow(const std::string &table, const Json &row)
    {
        auto *statement = Prepare(table, row.size());
        for (std::size_t i = 0; i < row.size(); ++i)
        {
            BindValue(statement, static_cast<int>(i + 1), row[i]);
        }
        FinishRow(statement);
    }

    void CreateExtension(const std::string &table, const schema::TableColumns &columns)
    {
        CreateTable(table, columns);
        Prepare(table, columns.size(), &columns);
    }

    std::uint64_t StringId(const std::string &text)
    {
        if (const auto found = strings_.find(text); found != strings_.end())
        {
            return found->second;
        }
        const auto [position, inserted] = strings_.emplace(text, strings_.size());
        if (inserted)
        {
            Insert(schema::TABLE_STRING_IDS, position->second, text);
        }
        return position->second;
    }

    void Commit()
    {
        statements_.clear();
        Execute("COMMIT");
    }

   private:
    sqlite3_stmt *Prepare(const std::string &table, std::size_t width, const schema::TableColumns *extension = nullptr)
    {
        auto &statement = statements_[table];
        if (!statement)
        {
            const auto &columns = extension ? *extension : schema_.GetTableCols(table);
            if (columns.empty() || columns.size() != width)
            {
                throw std::runtime_error("DB schema mismatch: " + table);
            }
            std::string insert = "INSERT INTO " + table + " VALUES (";
            for (std::size_t i = 0; i < width; ++i)
            {
                insert += i ? ",?" : "?";
            }
            sqlite3_stmt *raw = nullptr;
            const auto status = sqlite3_prepare_v2(database_.get(), (insert + ")").c_str(), -1, &raw, nullptr);
            statement.reset(raw);
            Check(status);
        }
        if (static_cast<std::size_t>(sqlite3_bind_parameter_count(statement.get())) != width)
        {
            throw std::runtime_error("DB row width mismatch: " + table);
        }
        return statement.get();
    }

    void FinishRow(sqlite3_stmt *statement)
    {
        const auto status = sqlite3_step(statement);
        if (status != SQLITE_DONE)
        {
            throw std::runtime_error(sqlite3_errmsg(database_.get()));
        }
        // Release SQLITE_STATIC text bindings before the caller's row values expire.
        Check(sqlite3_reset(statement));
        Check(sqlite3_clear_bindings(statement));
    }

    void BindValue(sqlite3_stmt *statement, int index, std::string_view value)
    {
        Check(sqlite3_bind_text64(statement, index, value.data(), value.size(), SQLITE_STATIC, SQLITE_UTF8));
    }

    void BindValue(sqlite3_stmt *statement, int index, const std::string &value)
    {
        BindValue(statement, index, std::string_view(value));
    }

    void BindValue(sqlite3_stmt *statement, int index, const char *value)
    {
        BindValue(statement, index, std::string_view(value));
    }

    template <typename Integer, std::enable_if_t<std::is_integral_v<Integer>, int> = 0>
    void BindValue(sqlite3_stmt *statement, int index, Integer value)
    {
        if constexpr (std::is_unsigned_v<Integer>)
        {
            if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
            {
                throw std::runtime_error("unsigned value exceeds SQLite integer range");
            }
        }
        Check(sqlite3_bind_int64(statement, index, value));
    }

    void BindValue(sqlite3_stmt *statement, int index, const Json &value)
    {
        if (value.is_string())
        {
            BindValue(statement, index, std::string_view(value.get_ref<const std::string &>()));
        }
        else if (value.is_number_unsigned())
        {
            BindValue(statement, index, value.get<std::uint64_t>());
        }
        else if (value.is_number_integer())
        {
            BindValue(statement, index, value.get<std::int64_t>());
        }
        else
        {
            throw std::runtime_error("unsupported DB field type");
        }
    }

    void CreateTable(const std::string &table, const schema::TableColumns &columns)
    {
        std::string sql = "CREATE TABLE IF NOT EXISTS " + table + " (";
        for (std::size_t i = 0; i < columns.size(); ++i)
        {
            sql += (i ? "," : "") + columns[i].ToString();
        }
        Execute(sql + ")");
    }

    void Check(int status)
    {
        if (status != SQLITE_OK)
        {
            throw std::runtime_error(database_ ? sqlite3_errmsg(database_.get()) : "cannot allocate SQLite connection");
        }
    }

    std::unique_ptr<sqlite3, decltype(&sqlite3_close)> database_{nullptr, sqlite3_close};
    struct StatementDeleter
    {
        void operator()(sqlite3_stmt *statement) const { sqlite3_finalize(statement); }
    };
    std::map<std::string, std::unique_ptr<sqlite3_stmt, StatementDeleter>> statements_;
    schema::MsMonitorDB schema_;
    std::unordered_map<std::string, std::uint64_t> strings_;
};

std::uint64_t GlobalThread(const Json &data)
{
    return (data.value("processId", std::uint64_t{0}) << 32) | data.value("threadId", std::uint64_t{0});
}

void WriteKernel(StepDatabase &db, const StepEvent &event, std::uint64_t taskId, std::uint64_t processId)
{
    const auto &data = event.data;
    const auto name = db.StringId(data.at("name"));
    const auto type = db.StringId(data.at("type"));
    const auto missing = db.StringId("N/A");
    const auto unknown = DbConstants::UnknownId;
    db.Insert(schema::TABLE_COMPUTE_TASK_INFO, name, taskId, unknown, unknown, type, missing, missing, missing, missing,
              missing, missing, missing, missing, missing, missing);
    db.Insert(schema::TABLE_TASK, event.startNs, event.endNs, data.at("deviceId"),
              data.value("correlationId", std::uint64_t{0}), taskId, processId, type, unknown, data.at("streamId"),
              unknown, unknown);
}

void WriteCommunication(StepDatabase &db, const StepEvent &event, std::uint64_t opId)
{
    const auto &data = event.data;
    const auto name = data.at("name").get<std::string>();
    const auto group = data.value("commName", "unknown");
    const auto opName = db.StringId(name + "_" + group + "_" + std::to_string(opId));
    const auto &types = CommunicationTypes();
    const auto typeName = data.value("dataType", "INVALID_TYPE");
    const auto found =
        std::find_if(types.begin(), types.end(), [&](const auto &type) { return type.second == typeName; });
    const auto type = found == types.end() ? types.back().first : found->first;
    db.Insert(schema::TABLE_COMMUNICATION_OP, opName, event.startNs, event.endNs,
              data.value("correlationId", std::uint64_t{0}), db.StringId(group), opId, 0, 0, type,
              db.StringId(data.value("algType", "unknown")), data.value("count", std::uint64_t{0}), db.StringId(name),
              data.at("deviceId"));
}

void WriteMarker(StepDatabase &db, const StepEvent &event, std::uint64_t taskId, std::uint64_t processId)
{
    const auto &data = event.data;
    const auto connection = data.value("id", std::uint64_t{0});
    if (connection > std::numeric_limits<std::uint64_t>::max() - DbConstants::MarkerConnectionOffset)
    {
        throw std::runtime_error("marker connection ID overflow");
    }
    const auto id = connection + DbConstants::MarkerConnectionOffset;
    const auto unknown = DbConstants::UnknownId;
    if (data.value("sourceKind", "") == "Host")
    {
        db.Insert(schema::TABLE_MSTX, event.startNs, event.endNs, event.startNs == event.endNs ? 0 : 2, unknown,
                  unknown, db.StringId(data.value("name", "")), GlobalThread(data), GlobalThread(data),
                  db.StringId(data.value("domain", "default")), id);
    }
    else
    {
        db.Insert(schema::TABLE_TASK, event.startNs, event.endNs, data.at("deviceId"), id, taskId, processId,
                  db.StringId("MsTx"), unknown, data.at("streamId"), unknown, unknown);
    }
}

void WriteActivity(StepDatabase &db, const StepEvent &event, std::uint64_t taskId, std::uint64_t processId)
{
    const auto &data = event.data;
    const auto kind = data.value("kind", "");
    if (kind == "Kernel")
    {
        WriteKernel(db, event, taskId, processId);
    }
    else if (kind == "Communication")
    {
        WriteCommunication(db, event, taskId);
    }
    else if (kind == "Marker")
    {
        WriteMarker(db, event, taskId, processId);
    }
    else if (kind == "API" || kind == "AclAPI" || kind == "NodeAPI" || kind == "RuntimeAPI")
    {
        const auto level = kind == "AclAPI" ? 20000 : (kind == "RuntimeAPI" ? 5000 : 10000);
        db.Insert(schema::TABLE_CANN_API, event.startNs, event.endNs, level, GlobalThread(data),
                  data.value("correlationId", std::uint64_t{0}), db.StringId(data.value("name", "")));
    }
    else
    {
        throw std::runtime_error("unsupported activity for MindStudio DB: " + kind);
    }
}

void WriteMetadata(StepDatabase &db, const StepDataOutput &step, std::uint64_t processId,
                   const std::set<std::uint64_t> &devices, std::uint64_t sessionStart, std::uint64_t sessionEnd)
{
    // Include full crossing events in the file's visible range; metadata keeps the exact step window.
    db.Insert(schema::TABLE_SESSION_TIME_INFO, sessionStart, sessionEnd);
    for (const auto &row :
         std::vector<Json>{{"SCHEMA_VERSION_MICRO", "1"},
                           {"SCHEMA_VERSION_MINOR", "2"},
                           {"SCHEMA_VERSION_MAJOR", "0"},
                           {"SCHEMA_VERSION", "1.2.0"},
                           {"msmonitor_step_id", std::to_string(step.boundary.stepId)},
                           {"msmonitor_step_start_ns", std::to_string(step.boundary.startNs)},
                           {"msmonitor_step_end_ns", std::to_string(step.boundary.endNs)},
                           {"msmonitor_step_completion", std::to_string(static_cast<int>(step.completion))},
                           {"msmonitor_process_id_known", processId ? "true" : "false"},
                           {"msmonitor_metadata_scope", "event-only; host identity and parallel groups not provided"}})
    {
        db.InsertRow(schema::TABLE_META_DATA, row);
    }
    for (const auto device : devices)
    {
        db.Insert(schema::TABLE_RANK_DEVICE_MAP, step.rank, device);
        db.Insert(schema::TABLE_NPU_INFO, device, "UNKNOWN");
    }
    for (const auto &row : std::vector<Json>{{0, "marker"}, {1, "push/pop"}, {2, "start/end"}, {3, "marker_ex"}})
    {
        db.InsertRow(schema::TABLE_MSTX_EVENT_TYPE, row);
    }
    for (const auto &row : std::vector<Json>{{5000, "runtime"},
                                             {5500, "communication"},
                                             {10000, "node"},
                                             {15000, "model"},
                                             {20000, "acl"},
                                             {50001, "op"},
                                             {50002, "queue"},
                                             {50003, "trace"},
                                             {50004, "mstx"}})
    {
        db.InsertRow(schema::TABLE_API_TYPE, row);
    }
    for (const auto &[id, name] : CommunicationTypes())
    {
        db.Insert(schema::TABLE_HCCL_DATA_TYPE, id, name);
    }
}

void WriteStepAnalysis(StepDatabase &db, const StepDataOutput &step)
{
    // Derived scopes use a private extension table, never masquerade as collected TASK rows.
    const schema::TableColumns derived{
        {"deviceId", "INTEGER"}, {"scope", "TEXT"}, {"startNs", "INTEGER"}, {"endNs", "INTEGER"}};
    db.CreateExtension("MSMONITOR_STEP_ANALYSIS", derived);
    const auto analysis = step.analysis ? step.analysis : AnalyseStepIntervals(step);
    for (const auto &[device, ranges] : analysis->devices)
    {
        for (const auto &[scope, intervals] : std::vector<std::pair<const char *, const Intervals *>>{
                 {"computing", &ranges.computing},
                 {"communication", &ranges.communication},
                 {"overlap", &ranges.overlap},
                 {"uncovered_communication", &ranges.uncoveredCommunication},
                 {"observed_free", &ranges.free}})
        {
            for (const auto &[start, end] : *intervals)
            {
                db.Insert("MSMONITOR_STEP_ANALYSIS", device, scope, start, end);
            }
        }
    }
}

}  // namespace

void WriteMindStudioDb(const StepDataOutput &step, const std::filesystem::path &file)
{
    StepDatabase db(file);
    std::set<std::uint64_t> devices;
    std::uint64_t processId = 0;
    auto sessionStart = step.boundary.startNs;
    auto sessionEnd = step.boundary.endNs;
    for (const auto &event : step.events)
    {
        if (event->data.contains("processId"))
        {
            processId = event->data.at("processId").get<std::uint64_t>();
            break;
        }
    }
    std::uint64_t taskId = 0;
    for (const auto &event : step.events)
    {
        const auto &data = event->data;
        sessionStart = std::min(sessionStart, event->startNs);
        sessionEnd = std::max(sessionEnd, event->endNs);
        if (data.contains("deviceId"))
        {
            devices.insert(data.at("deviceId").get<std::uint64_t>());
        }
        WriteActivity(db, *event, taskId++, processId);
    }
    WriteMetadata(db, step, processId, devices, sessionStart, sessionEnd);
    WriteStepAnalysis(db, step);
    db.Commit();
}
}  // namespace msmonitor::process

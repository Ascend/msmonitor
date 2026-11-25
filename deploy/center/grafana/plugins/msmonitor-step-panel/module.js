System.register(['react', '@grafana/data', '@grafana/runtime', '@grafana/ui'], function (exports) {
  let React, PanelPlugin, RefreshEvent, config, getBackendSrv, getTemplateSrv, locationService, MultiCombobox, useTheme2;
  let h;

  const API = '/api/datasources/proxy/uid/msmonitor-summary/summary/';
  const GROUP_TYPES = [['', '全部'], ['dp', 'DP'], ['pp', 'PP'], ['tp', 'TP']];
  const RANK_LIMITS = [['10', '10'], ['20', '20']];
  const PALETTE = ['#5794f2', '#73bf69', '#f2cc0c', '#b877d9', '#ff9830', '#56c0a9'];
  const requests = new Map();

  function number(value, digits = 2) {
    return value == null ? '—' : new Intl.NumberFormat('zh-CN', { maximumFractionDigits: digits }).format(value);
  }

  function percent(value) {
    return value == null ? '—' : `${number(value * 100)}%`;
  }

  function useRemote(endpoint, params, enabled = true) {
    const query = new URLSearchParams();
    for (const [name, value] of Object.entries(params)) {
      for (const entry of Array.isArray(value) ? value : [value]) {
        if (entry != null && entry !== '') query.append(name, entry);
      }
    }
    const url = enabled ? API + endpoint + '?' + query : '';
    const [state, setState] = React.useState({});
    React.useEffect(() => {
      if (!url) return undefined;
      let active = true;
      const timer = setTimeout(() => {
        if (!requests.has(url)) {
          const promise = getBackendSrv().get(url, undefined, undefined, { showErrorAlert: false })
            .finally(() => requests.delete(url));
          requests.set(url, promise);
        }
        requests.get(url).then(
          (data) => { if (active) setState({ url, data }); },
          (error) => { if (active) setState({ url, error: error.data?.message || error.message || '查询失败' }); },
        );
      }, 120);
      return () => { active = false; clearTimeout(timer); };
    }, [url]);

    return state.url === url ? state : { pending: Boolean(url) };
  }

  function Hint({ children, error = false }) {
    return h('div', { className: 'msm-hint' + (error ? ' msm-error' : ''), role: error ? 'alert' : undefined }, children);
  }

  function State({ query, children }) {
    if (query.error) return h(Hint, { error: true }, query.error);
    if (query.pending) return h(Hint, null, '正在查询…');
    return query.data ? children(query.data) : null;
  }

  function Select({ label, value, change, options, disabled }) {
    return h('label', { className: 'msm-field' }, label,
      h('select', { value, onChange: (event) => change(event.target.value), disabled, 'aria-label': label },
        options.map(([key, text]) => h('option', { key, value: key }, text))));
  }

  function Input({ label, value, change, placeholder, type = 'text', width }) {
    return h('label', { className: 'msm-field' }, label,
      h('input', {
        value, type, placeholder, onChange: (event) => change(event.target.value),
        style: width ? { width } : undefined, min: type === 'number' ? 0 : undefined,
      }));
  }

  function Controls({ children }) {
    return h('div', { className: 'msm-controls' }, children);
  }

  function paintChart(canvas, { rows, series, unit, colors, hover, selected, unitOnAxis }) {
    const width = canvas.clientWidth;
    const height = canvas.clientHeight;
    if (width < 120 || !height || !rows.length) return undefined;
    const ratio = window.devicePixelRatio || 1;
    canvas.width = Math.round(width * ratio);
    canvas.height = Math.round(height * ratio);
    const ctx = canvas.getContext('2d');
    ctx.scale(ratio, ratio);
    ctx.font = '11px system-ui, sans-serif';
    const padding = { top: 14, right: 20, bottom: 34, left: unitOnAxis ? 74 : 58 };
    const first = rows[0].step;
    const last = rows[rows.length - 1].step;
    const plotWidth = width - padding.left - padding.right;
    const plotHeight = height - padding.top - padding.bottom;
    const bottom = padding.top + plotHeight;
    const x = (step) => padding.left + (last === first ? 0.5 : (step - first) / (last - first)) * plotWidth;
    let maximum = 0;
    for (const row of rows) {
      for (const line of series) maximum = Math.max(maximum, line.get(row) ?? 0);
    }
    const maxY = unit === '%' ? 100 : maximum ? maximum * (unit === 'Rank' ? 1 : 1.08) : 1;
    const y = (value) => bottom - value / maxY * plotHeight;
    const gridCount = unit === 'Rank' && maxY < 4 ? Math.ceil(maxY) : 4;
    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';
    for (let tick = 0; tick <= gridCount; tick++) {
      const value = maxY * tick / gridCount;
      ctx.strokeStyle = colors.border;
      ctx.beginPath();
      ctx.moveTo(padding.left, y(value));
      ctx.lineTo(width - padding.right, y(value));
      ctx.stroke();
      ctx.fillStyle = colors.muted;
      ctx.fillText(number(value, 3) + (unit === '%' ? '%' : ''), padding.left - 7, y(value));
    }

    if (unitOnAxis) {
      ctx.save();
      ctx.translate(12, padding.top + plotHeight / 2);
      ctx.rotate(-Math.PI / 2);
      ctx.textAlign = 'center';
      ctx.fillText(unit, 0, 0);
      ctx.restore();
    }

    ctx.textAlign = 'center';
    ctx.fillStyle = colors.muted;
    const ticks = Math.max(1, Math.min(8, last - first, Math.floor(plotWidth / 75)));
    for (const step of new Set(Array.from({ length: ticks + 1 }, (_, tick) => Math.round(first + (last - first) * tick / ticks)))) {
      ctx.fillText(String(step), x(step), bottom + 12);
    }
    ctx.fillText('Step', padding.left + plotWidth / 2, height - 7);

    for (const [index, line] of series.entries()) {
      ctx.strokeStyle = ctx.fillStyle = line.color ?? PALETTE[index % PALETTE.length];
      ctx.lineWidth = 1.8;
      let started = false;
      ctx.beginPath();
      for (const row of rows) {
        const value = line.get(row);
        if (!Number.isFinite(value)) continue;
        const pointX = x(row.step);
        const pointY = y(value);
        if (line.bar) {
          const barWidth = Math.min(24, Math.max(2, 0.65 * plotWidth / Math.max(1, last - first + 1)));
          ctx.fillRect(pointX - barWidth / 2, Math.min(pointY, bottom - 2), barWidth, Math.max(2, bottom - pointY));
        } else if (started) {
          ctx.lineTo(pointX, pointY);
        } else {
          ctx.moveTo(pointX, pointY);
          started = true;
        }
      }
      ctx.stroke();
      if (!line.bar) {
        for (const [index, row] of rows.entries()) {
          const value = line.get(row);
          if (!Number.isFinite(value)) continue;
          const previous = rows[index - 1];
          const next = rows[index + 1];
          const connected = (previous?.step === row.step - 1 && Number.isFinite(line.get(previous))) ||
            (next?.step === row.step + 1 && Number.isFinite(line.get(next)));
          if (rows.length > 160 && connected) continue;
          ctx.beginPath();
          ctx.arc(x(row.step), y(value), rows.length < 60 ? 2.6 : 1.6, 0, 2 * Math.PI);
          ctx.fill();
        }
      }
    }

    const focused = hover ?? selected;
    if (focused != null) {
      ctx.strokeStyle = colors.muted;
      ctx.setLineDash([3, 4]);
      ctx.beginPath();
      ctx.moveTo(x(focused), padding.top);
      ctx.lineTo(x(focused), bottom);
      ctx.stroke();
    }
    return { x, first, last };
  }

  function nearest(rows, axis, cursor) {
    let low = 0;
    let high = rows.length - 1;
    while (low < high) {
      const current = (low + high) >> 1;
      if (axis(rows[current].step) < cursor) low = current + 1;
      else high = current;
    }
    if (low > 0 && Math.abs(axis(rows[low - 1].step) - cursor) < Math.abs(axis(rows[low].step) - cursor)) low--;
    return rows[low];
  }

  function Chart({ rows, series, unit, colors, detail, link, label, unitOnAxis = false, hoverStep, onHoverStep }) {
    const canvas = React.useRef(null);
    const axis = React.useRef(null);
    const [hover, setHover] = React.useState(null);
    const [selected, setSelected] = React.useState(null);
    const selectedRow = selected == null ? null : rows.find((row) => row.step === selected);
    const current = hover?.row ?? selectedRow;
    React.useLayoutEffect(() => {
      const node = canvas.current;
      if (!node) return undefined;
      const draw = () => {
        axis.current = paintChart(node, { rows, series, unit, colors, hover: hover?.row.step ?? hoverStep, selected, unitOnAxis });
      };
      const observer = new ResizeObserver(draw);
      observer.observe(node);
      draw();
      return () => observer.disconnect();
    }, [rows, series, unit, colors, hover, hoverStep, selected, unitOnAxis]);

    if (!rows.length) return h(Hint, null, '所选范围内没有 Step 样本。');
    const move = (event) => {
      if (!axis.current) return;
      const node = event.currentTarget;
      const cursor = event.clientX - node.getBoundingClientRect().left;
      const row = nearest(rows, axis.current.x, cursor);
      const left = Math.max(4, Math.min(axis.current.x(row.step) + 12, node.clientWidth - 228));
      setHover({ row, left });
      onHoverStep?.(row.step);
    };
    return h('div', { className: 'msm-chart' },
      h('div', { className: 'msm-chart-plot' },
        h('canvas', {
          ref: canvas, 'aria-label': unitOnAxis ? `${label}（${unit}）` : label, role: 'img',
          onMouseMove: move, onMouseLeave: () => { setHover(null); onHoverStep?.(null); },
          onClick: () => { if (hover) setSelected(hover.row.step); },
        }),
        hover && h('div', { className: 'msm-tooltip', style: { left: hover.left } },
          h('strong', null, `Step ${hover.row.step}`),
          detail(hover.row).map((line) => h('div', { key: line }, line)))),
      h('div', { className: 'msm-legend' },
        series.map((line, index) => h('span', { key: line.label },
          h('i', { style: { background: line.color ?? PALETTE[index % PALETTE.length] } }), line.label)),
        unit !== '%' && !unitOnAxis && h('span', { className: 'msm-muted' }, unit),
        selectedRow && link && h('a', { href: link(selectedRow.step), className: 'msm-link' }, `查看 Step ${selectedRow.step} 的算子分析 →`)),
      current && !hover && h('div', { className: 'msm-muted' }, `Step ${current.step} · ${detail(current).join(' · ')}`));
  }

  function L1Overview({ scope, mode, colors }) {
    const query = useRemote('overview', scope);
    const names = { mean: '平均耗时', spread: '快慢差', missing: '缺报 Rank 数' };
    const detail = (row) => {
      const counts = `实报 ${number(row.count)} / 应报 ${row.expected == null ? '未知' : number(row.expected)}`;
      if (mode === 'mean') return [`平均耗时 ${number(row.mean)} ms`, counts];
      if (mode === 'missing') return [counts, `缺报 ${row.missing == null ? '未知' : number(row.missing)}`, row.status];
      if (row.spread == null) return ['数据不足', counts];
      return [
        `最快 Rank ${row.fast.rank} · ${number(row.fast.ms)} ms`,
        `最慢 Rank ${row.slow.rank} · ${number(row.slow.ms)} ms`,
        `快慢差 ${number(row.spread)} ms`,
        row.status === '已收齐' ? counts : `按 ${number(row.count)} 个已上报 Rank 统计`,
      ];
    };
    const link = (step) => {
      const params = new URLSearchParams({ from: scope.from, to: scope.to, 'var-job': scope.job, 'var-run': scope.run, 'var-step': step });
      return (config.appSubUrl || '') + '/d/msmonitor-l3?' + params;
    };
    return h(State, { query }, ({ rows }) => h(React.Fragment, null,
      mode === 'missing' && rows.length > 0 && rows.every((row) => row.missing == null) &&
        h('div', { className: 'msm-muted' }, '缺报数未知 · 悬停查看实报数及上报状态'),
      h(Chart, {
        rows, colors, detail, link: mode === 'mean' ? link : undefined, label: names[mode],
        series: [{ label: names[mode], get: (row) => row[mode], bar: mode === 'missing' }],
        unit: mode === 'missing' ? 'Rank' : 'ms',
      })));
  }

  function SingleRank({ scope, colors }) {
    const [rank, setRank] = React.useState('0');
    const [all, setAll] = React.useState(false);
    const [first, setFirst] = React.useState('');
    const [last, setLast] = React.useState('');
    const validRank = /^[0-9]+$/.test(rank);
    const validRange = /^[0-9]*$/.test(first) && /^[0-9]*$/.test(last) && (!first || !last || +first <= +last);
    const query = useRemote('rank-trend', { ...scope, rank, all }, validRank);
    return h(React.Fragment, null,
      h(Controls, null,
        h(Input, { label: 'Rank ID', value: rank, change: setRank, type: 'number', width: 96 }),
        h(Select, { label: 'Step 范围', value: String(all), change: (value) => setAll(value === 'true'), options: [['false', '当前时间范围'], ['true', '全部保留 Step']] }),
        h(Input, { label: '起始 Step', value: first, change: setFirst, type: 'number', width: 96 }),
        h(Input, { label: '结束 Step', value: last, change: setLast, type: 'number', width: 96 })),
      !validRank && h(Hint, null, '请输入有效的 Rank ID。'),
      !validRange && h(Hint, null, '请输入有效的起止 Step。'),
      validRange && h(State, { query }, ({ rows }) => h(Chart, {
        rows: rows.filter((row) => (!first || row.step >= +first) && (!last || row.step <= +last)),
        colors, unit: 'ms', label: '单 Rank 与全体平均耗时',
        series: [{ label: `Rank ${rank}`, get: (row) => row.duration }, { label: '各 Rank 平均', get: (row) => row.mean }],
        detail: (row) => [`Rank ${rank} · ${row.duration == null ? '未上报' : number(row.duration) + ' ms'}`, `各 Rank 平均 · ${number(row.mean)} ms`],
      })));
  }

  function Recent({ scope }) {
    const [now, setNow] = React.useState(Date.now());
    React.useEffect(() => {
      const timer = setInterval(() => setNow(Date.now()), 1000);
      return () => clearInterval(timer);
    }, []);
    const query = useRemote('recent', { job: scope.job, run: scope.run, refresh: Math.floor(now / 15000) });
    return h(State, { query }, ({ received, state }) => {
      const seconds = received == null ? null : Math.max(0, Math.floor(now / 1000 - received));
      const display = seconds == null ? '未知' : seconds >= 3600
        ? `${Math.floor(seconds / 3600)} 小时 ${Math.floor(seconds % 3600 / 60)} 分`
        : seconds >= 60 ? `${Math.floor(seconds / 60)} 分 ${seconds % 60} 秒` : `${seconds} 秒`;
      return h('div', { className: 'msm-stat' },
        h('div', { className: 'msm-muted' }, '距最近一次收到 Step 耗时'),
        h('strong', null, display),
        h('div', { className: 'msm-muted' }, `作业状态 · ${state || '未知'}`));
    });
  }

  function L2({ scope, colors, mode }) {
    const coverage = mode === 'coverage';
    const [group, setGroup] = React.useState('');
    const [filters, setFilters] = React.useState({});
    const { name = '', source = '', ranks = '', steps = '' } = filters;
    const changeFilter = (key) => (value) => setFilters((current) => ({ ...current, [key]: value }));
    const domains = useRemote('groups', scope);
    const choices = [['', '全部 Rank'], ...(domains.data?.groups || []).map((entry) => [entry.id, entry.id])];
    const chosenGroup = choices.some(([value]) => value === group) ? group : '';
    const params = coverage ? { group: chosenGroup, steps, ranks } : { group: chosenGroup, source };
    const query = useRemote(coverage ? 'coverage' : 'ranges', { ...scope, ...params });
    const nameChoices = [['', '全部打点'], ...(query.data?.names || []).map((entry) => [entry, entry])];
    const selectedName = nameChoices.some(([value]) => value === name) ? name : '';

    return h(React.Fragment, null,
      h(Controls, null,
        h(Select, { label: '并行域', value: chosenGroup, change: setGroup, options: choices }),
        coverage ? h(React.Fragment, null,
          h(Input, { label: 'Rank', value: ranks, change: changeFilter('ranks'), placeholder: '全部，如 1,2', width: 120 }),
          h(Input, { label: 'Step', value: steps, change: changeFilter('steps'), placeholder: '全部，如 12,14', width: 120 }))
          : h(React.Fragment, null,
            h(Select, { label: '打点名称', value: selectedName, change: changeFilter('name'), options: nameChoices, disabled: !query.data }),
            h(Select, { label: '采集来源', value: source, change: changeFilter('source'), options: [['', '全部'], ['Host', 'Host'], ['Device', 'Device']] }))),
      domains.error && h(Hint, { error: true }, domains.error),
      h(State, { query }, ({ rows, names }) => {
        const coverageLabels = {
          overlap: '通信与计算重叠',
          uncovered_communication: '未掩盖通信',
          communication: '通信活跃',
          kernel_active: 'Kernel 活跃',
          observed_free: '观测空闲',
        };
        const keys = coverage ? Object.keys(coverageLabels) : selectedName ? [selectedName] : names;
        const label = (key) => coverage ? coverageLabels[key] : key;
        const series = keys.map((key) => ({
          label: label(key),
          get: (row) => row.values[key] ? row.values[key].value * (coverage ? 100 : 1) : null,
        }));
        const detail = (row) => keys.map((key) => {
          const value = row.values[key];
          if (!value) return `${label(key)} · 无有效样本`;
          const text = coverage ? percent(value.value) : `${number(value.value, 4)} ms · ${number(value.count)} 次`;
          return `${label(key)} · ${text} · ${number(value.ranks)} 个 Rank`;
        });
        if (!rows.some((row) => keys.some((key) => row.values[key]))) return h(Hint, null, '筛选范围内没有符合条件的样本。');
        return h(Chart, {
          rows, series, colors, detail,
          unit: coverage ? '%' : 'ms',
          unitOnAxis: !coverage,
          label: coverage ? '通算掩盖占比' : '每次调用平均耗时',
        });
      }));
  }

  function DomainBar({ value, scale, children }) {
    const width = value != null && scale > 0 ? Math.abs(value) / scale * 50 : 0;
    return h('span', { className: 'msm-table-bar' },
      width > 0 && h('i', {
        className: value < 0 ? 'msm-negative' : '',
        style: { left: `${value < 0 ? 50 - width : 50}%`, width: `${width}%` },
      }),
      h('span', null, children));
  }

  function DomainTable({ scope, mode }) {
    const [type, setType] = React.useState('');
    const [search, setSearch] = React.useState('');
    const [page, setPage] = React.useState(1);
    const [sort, setSort] = React.useState({ field: mode, desc: true });
    const query = useRemote(mode, { ...scope, type, search, page, sort: sort.field, desc: sort.desc });
    const columns = mode === 'tail' ? [
      ['group', '通信域'], ['type', '类型'], ['count', '有效卡数'], ['median', '中位数 · ms'],
      ['slow', '最慢 Rank'], ['slowMs', '最慢 · ms'], ['tail', '正尾差 · ms'],
    ] : [
      ['group', '通信域'], ['type', '类型'], ['count', '有效卡数'], ['median', '中位数 · ms'],
      ['baseline', '同类域基线 · ms'], ['offset', '整体偏移 · ms'], ['rate', '偏移率'],
    ];

    function cell(row, field) {
      if (field === 'type') return row.type.toUpperCase();
      if (field === 'group') return row.group;
      if (field === 'baseline' && row.baseline == null) return '无可比基线';
      if (field === 'rate') return row.baseline === 0 ? '不可计算' : percent(row.rate);
      return number(row[field]);
    }

    return h(React.Fragment, null,
      h(Controls, null,
        h(Select, { label: '通信域类型', value: type, change: (value) => { setType(value); setPage(1); }, options: GROUP_TYPES }),
        h(Input, { label: '搜索', value: search, change: (value) => { setSearch(value); setPage(1); }, placeholder: '通信域名称', width: 170 })),
      h(State, { query }, ({ rows, total, page: current, scale, status }) => {
        const header = columns.map(([key, title]) => {
          const active = sort.field === key;
          return h('th', { key, scope: 'col', 'aria-sort': active ? (sort.desc ? 'descending' : 'ascending') : 'none' },
            h('button', { type: 'button', onClick: () => { setSort({ field: key, desc: active ? !sort.desc : true }); setPage(1); } },
              title, active ? (sort.desc ? ' ↓' : ' ↑') : ''));
        });
        const body = rows.map((row) => h('tr', { key: row.group }, columns.map(([key]) => {
          const value = cell(row, key);
          const bar = sort.field === key && typeof row[key] === 'number';
          return h('td', { key }, bar ? h(DomainBar, { value: row[key], scale }, value) : value);
        })));
        const table = h('div', { className: 'msm-table-wrap' }, h('table', { className: 'msm-table' },
          h('thead', null, h('tr', null, header)), h('tbody', null, body)));
        return h(React.Fragment, null,
          status && h(Hint, null, status),
          rows.length ? table : !status && h(Hint, null, '没有符合条件的通信域。'),
          total > 0 && h('div', { className: 'msm-pagination' },
            h('span', { className: 'msm-muted' }, `共 ${number(total)} 个域 · 第 ${current} / ${Math.ceil(total / 20)} 页`),
            h('button', { type: 'button', disabled: current <= 1, onClick: () => setPage(current - 1) }, '上一页'),
            h('button', { type: 'button', disabled: current * 20 >= total, onClick: () => setPage(current + 1) }, '下一页')));
      }));
  }

  function RankOffsetBars({ rows, showDuration = false }) {
    const extent = Math.max(0.001, ...rows.map((row) => Math.abs(row.offset)));
    return h('div', { className: 'msm-bars msm-rank-offsets' + (showDuration ? ' msm-rank-totals' : '') },
      showDuration && h('div', { className: 'msm-bar-row msm-axis' },
        h('span', null, 'Rank'), h('span', null),
        h('span', { className: 'msm-rank-value' }, '累计耗时 · ms'), h('span', null, '偏移 · ms')),
      rows.map((row) => {
        const width = Math.abs(row.offset) / extent * 50;
        const offset = `${row.offset > 0 ? '+' : ''}${number(row.offset, 3)}`;
        return h('div', {
          className: 'msm-bar-row', key: row.rank,
          title: `Rank ${row.rank} · 累计 ${number(row.ms, 3)} ms · 偏移 ${offset} ms`,
        },
        h('span', null, `Rank ${row.rank}`),
        h('div', { className: 'msm-track', style: { '--msm-zero': '50%' } },
          h('i', { className: row.offset < 0 ? 'msm-negative' : '', style: { left: `${row.offset < 0 ? 50 - width : 50}%`, width: `${width}%` } })),
        showDuration && h('span', { className: 'msm-rank-value' }, number(row.ms, 3)),
        h('span', { className: 'msm-rank-value' }, showDuration ? offset : `${offset} ms`));
      }),
      h('div', { className: 'msm-bar-row msm-axis' }, h('span', null, showDuration ? '' : 'Rank'),
        h('div', { className: 'msm-axis-labels' },
          h('span', null, `−${number(extent, 3)}`), h('span', null, '0'), h('span', null, number(extent, 3))),
        !showDuration && h('span', null, '偏移 · ms')));
  }

  function TopRanks({ scope }) {
    const [type, setType] = React.useState('tp');
    const [group, setGroup] = React.useState('');
    const [operator, setOperator] = React.useState('');
    const [basis, setBasis] = React.useState('local');
    const [limit, setLimit] = React.useState('10');
    const [threshold, setThreshold] = React.useState('10');
    const domains = useRemote('groups', scope);
    const names = useRemote('operators', scope);
    const groups = (domains.data?.groups || []).filter((item) => item.type === type).map((item) => [item.id, item.id]);
    const chosen = groups.some(([id]) => id === group) ? group : groups[0]?.[0] || '';
    const operators = [['', '所有算子'], ...(names.data?.operators || []).map((name) => [name, name])];
    const chosenOperator = operators.some(([id]) => id === operator) ? operator : '';
    const query = useRemote('top-ranks', { ...scope, group: chosen, operator: chosenOperator, basis, limit, threshold, direction: 'both' },
      Boolean(chosen) && threshold !== '' && +threshold >= 0);

    return h(React.Fragment, null,
      h(Controls, null,
        h(Select, { label: '通信域类型', value: type, change: setType, options: GROUP_TYPES.slice(1) }),
        h(Select, { label: '通信域', value: chosen, change: setGroup, options: groups.length ? groups : [['', '无可用域']], disabled: !groups.length }),
        h(Select, { label: '算子范围', value: chosenOperator, change: setOperator, options: operators }),
        h(Select, { label: '比较基线', value: basis, change: setBasis, options: [['local', '本域中位数'], ['peers', '其他同类域基线']] }),
        h(Select, { label: 'Top N', value: limit, change: setLimit, options: RANK_LIMITS }),
        h(Input, { label: '偏移阈值 · ms', value: threshold, change: setThreshold, type: 'number', width: 95 })),
      domains.error && h(Hint, { error: true }, domains.error),
      names.error && h(Hint, { error: true }, names.error),
      domains.data && !chosen && h(Hint, null, '当前 Step 没有该类型的通信域。'),
      h(State, { query }, ({ rows, count, baseline, positive, status }) => h(React.Fragment, null,
        h('div', { className: 'msm-metrics' },
          h('span', null, `有效卡数 ${number(count)}`),
          h('span', null, `基线 ${baseline == null ? '—' : number(baseline) + ' ms'}`),
          h('span', null, `正偏移超阈值 ${number(positive)} · ${count && baseline != null ? percent(positive / count) : '—'}`)),
        status && h(Hint, null, status),
        !status && !rows.length && h(Hint, null, '当前筛选条件下没有有效的 Rank 耗时样本。'),
        rows.length > 0 && h(RankOffsetBars, { rows }))));
  }

  function CommunicationRanks({ scope }) {
    const [operator, setOperator] = React.useState('');
    const [limit, setLimit] = React.useState('10');
    const names = useRemote('operators', { ...scope, kind: 'Communication' });
    const operators = [['', '全部通信算子'], ...(names.data?.operators || []).map((name) => [name, name])];
    const chosen = operators.some(([id]) => id === operator) ? operator : '';
    const query = useRemote('communication-ranks', { ...scope, operator: chosen, limit }, !operator || Boolean(names.data));
    return h(React.Fragment, null,
      h(Controls, null,
        h(Select, { label: '通信算子类型', value: chosen, change: setOperator, options: operators, disabled: !names.data }),
        h(Select, { label: 'Top N', value: limit, change: setLimit, options: RANK_LIMITS })),
      names.error && h(Hint, { error: true }, names.error),
      h(State, { query }, ({ rows, count, baseline }) => rows.length ? h(React.Fragment, null,
        h('div', { className: 'msm-metrics' },
          h('span', null, `实报 Rank 数 ${number(count)}`),
          h('span', null, `中位数 ${number(baseline, 3)} ms`)),
        h(RankOffsetBars, { rows, showDuration: true })) :
        h(Hint, null, '当前 Step 没有可用的通信算子耗时样本。')));
  }

  function KdeDistribution({ profiles }) {
    const streamCounts = new Map();
    for (const profile of profiles) streamCounts.set(profile.stream, (streamCounts.get(profile.stream) || 0) + 1);
    const groups = profiles.map((profile) => ({
      ...profile,
      label: streamCounts.get(profile.stream) > 1
        ? [profile.stream || '—', profile.name, profile.execution].filter(Boolean).join(' · ')
        : profile.stream || '—',
    }));
    const max = groups.reduce((limit, group) => Math.max(limit, ...group.clusters.map((row) => row.max)), 0.000001);
    const columns = ['Stream', '簇', '调用次数', '占比', '最小 · ms', 'P50 · ms', 'P99 · ms', '最大 · ms'];

    const chartRows = groups.map((group, index) => {
      const bars = group.clusters.map((row) => h('div', { className: 'msm-bar-row', key: row.id },
        h('span', null, `簇 ${row.id}`),
        h('div', { className: 'msm-kde-track' },
          h('i', { style: { left: `${row.min / max * 100}%`, width: `${(row.max - row.min) / max * 100}%` } }),
          h('b', { style: { left: `${row.p50 / max * 100}%` } }))));
      return h(React.Fragment, { key: index },
        h('div', { className: 'msm-kde-stream' }, h('span', null, `Stream ${group.label}`),
          group.calls != null && h('span', { className: 'msm-muted' }, `${number(group.calls)} 次调用`)),
        bars);
    });
    const tableRows = groups.flatMap((group, index) => group.clusters.map((row) => {
      const cells = [group.label, row.id, number(row.calls), percent(row.share),
        number(row.min, 5), number(row.p50, 5), number(row.p99, 5), number(row.max, 5)];
      return h('tr', { key: `${index}:${row.id}` }, cells.map((value, column) => h('td', { key: columns[column] }, value)));
    }));

    return h(React.Fragment, null,
      h('div', { className: 'msm-bars msm-kde', role: 'group', 'aria-label': 'KDE 分布，线段表示最小到最大耗时，圆点表示 P50' },
        chartRows,
        h('div', { className: 'msm-bar-row msm-axis' }, h('span', null, '耗时 · ms'),
          h('div', { className: 'msm-axis-labels' }, h('span', null, '0'), h('span', null, number(max / 2, 5)), h('span', null, number(max, 5))))),
      h('div', { className: 'msm-table-wrap msm-kde-table' },
        h('table', { className: 'msm-table', 'aria-label': 'KDE 簇明细' },
          h('thead', null, h('tr', null, columns.map((column) => h('th', { key: column, scope: 'col' }, column)))),
          h('tbody', null, tableRows))));
  }

  function Kde({ scope, mode }) {
    const kind = mode === 'kde-communication' ? 'Communication' : 'Kernel';
    const [rank, setRank] = React.useState('0');
    const [device, setDevice] = React.useState();
    const [operator, setOperator] = React.useState('');
    const [stream, setStream] = React.useState('');

    const rankQuery = useRemote('kde-options', { ...scope, kind });
    const ranks = rankQuery.data?.ranks || [];
    const chosenRank = ranks.includes(rank) ? rank : ranks[0];
    const picks = useRemote('kde-options', { ...scope, kind, rank: chosenRank }, Boolean(chosenRank));
    const choices = picks.data?.choices || [];
    const preferred = kind === 'Kernel' ? 'FlashAttentionScore' : 'hcom_allReduce_';
    const distinct = (values) => [...new Set(values)].sort((left, right) => left.localeCompare(right, undefined, { numeric: true }));
    const devices = distinct(choices.map((choice) => choice.device));
    const chosenDevice = devices.includes(device) ? device : devices[0];
    const deviceChoices = choices.filter((choice) => choice.device === chosenDevice);
    const names = distinct(deviceChoices.map((choice) => choice.operator));
    const chosenOperator = names.includes(operator) ? operator : names.includes(preferred) ? preferred : names[0];
    const streams = distinct(deviceChoices.filter((choice) => choice.operator === chosenOperator)
      .map((choice) => choice.stream).filter(Boolean));
    const chosenStream = streams.includes(stream) ? stream : '';
    const query = useRemote('kde', { ...scope, kind, rank: chosenRank, operator: chosenOperator }, Boolean(chosenOperator));
    const optionsQuery = chosenRank ? picks : rankQuery;
    const options = (values) => values.length ? values.map((value) => [value, value || '—']) : [['', '无可用项']];

    return h(React.Fragment, null,
      h(Controls, null,
        h(Select, {
          label: 'Rank ID', value: chosenRank ?? '', options: options(ranks), disabled: !ranks.length,
          change: (value) => { setRank(value); setStream(''); },
        }),
        h(Select, {
          label: '设备', value: chosenDevice ?? '', options: options(devices), disabled: !devices.length,
          change: (value) => { setDevice(value); setStream(''); },
        }),
        h(Select, {
          label: '算子类型', value: chosenOperator ?? '', options: options(names), disabled: !names.length,
          change: (value) => { setOperator(value); setStream(''); },
        }),
        h(Select, {
          label: 'Stream', value: chosenStream, change: setStream, disabled: !streams.length,
          options: [['', '全部 Stream'], ...streams.map((value) => [value, value])],
        })),
      optionsQuery.error && h(Hint, { error: true }, optionsQuery.error),
      optionsQuery.pending && h(Hint, null, '正在查询筛选项…'),
      optionsQuery.data && !chosenOperator && h(Hint, null, '当前筛选条件下没有该类别的算子样本。'),
      h(State, { query }, ({ profiles }) => {
        const selected = profiles.filter((profile) => profile.device === chosenDevice
          && (!chosenStream || profile.stream === chosenStream));
        return selected.length ? h(KdeDistribution, { profiles: selected })
          : h(Hint, null, '当前筛选条件下没有分簇数据。');
      }));
  }

  function OperatorTypeSelect({ label, names, value, change }) {
    const id = React.useId();
    return h('div', { className: 'msm-field msm-operator-type' },
      h('label', { htmlFor: id }, label),
      h(MultiCombobox, {
        id, placeholder: '选择算子类型', width: 32, disabled: !names.length, isClearable: true, value,
        options: names.map((name) => ({ label: name, value: name })),
        onChange: (options) => change(options.map((option) => option.value)),
      }));
  }

  function useOperatorTrend(scope, kind, names, preferred) {
    const [picked, change] = React.useState(null);
    const available = picked?.filter((name) => names.includes(name)) ?? [];
    const fallback = names.includes(preferred) ? preferred : names[0];
    const selected = available.length || picked?.length === 0 || !fallback ? available : [fallback];
    const query = useRemote('operator-trends', {
      ...scope, kind, operators: JSON.stringify(selected),
    }, selected.length > 0);
    return { names, selected, change, query };
  }

  function OperatorTrends({ scope, colors }) {
    const [rank, setRank] = React.useState('');
    const [hoverStep, setHoverStep] = React.useState(null);

    const rankQuery = useRemote('operator-trend-options', scope);
    const ranks = rankQuery.data?.ranks ?? [];
    const chosenRank = ranks.includes(rank) ? rank : '';
    const trendScope = { ...scope, rank: chosenRank };
    const rankTypes = useRemote('operator-trend-options', trendScope, Boolean(chosenRank));
    const typesQuery = chosenRank ? rankTypes : rankQuery;
    const types = typesQuery.data?.operators;
    const kernel = useOperatorTrend(trendScope, 'Kernel', types?.Kernel ?? [], 'MatMulV2');
    const communication = useOperatorTrend(trendScope, 'Communication', types?.Communication ?? [], 'hcom_allReduce_');
    const kernelData = kernel.query.data;
    const communicationData = communication.query.data;

    const rows = React.useMemo(() => {
      const byStep = new Map();
      for (const [field, data] of [['kernel', kernelData], ['communication', communicationData]]) {
        for (const [name, samples] of Object.entries(data?.series ?? {})) {
          for (const sample of samples) {
            const row = byStep.get(sample.step) ?? { step: sample.step, kernel: new Map(), communication: new Map() };
            row[field].set(name, sample);
            byStep.set(sample.step, row);
          }
        }
      }
      return [...byStep.values()].sort((left, right) => left.step - right.step);
    }, [kernelData, communicationData]);

    const sampleDetails = (title, names, samples) => names.map((name) => {
      const sample = samples.get(name);
      return sample ? `${title} · ${name} ${number(sample.ms, 3)} ms · ${sample.count} 个实报 Rank`
        : `${title} · ${name} · 无样本`;
    });
    const details = (row) => [
      ...sampleDetails('计算', kernel.selected, row.kernel),
      ...sampleDetails('通信', communication.selected, row.communication),
    ];
    const chart = (field, title, { names, selected, query }, colorOffset) => {
      const label = selected.length === 1 ? `${title} · ${selected[0]}` : title;
      return h('section', { className: 'msm-operator-trend', 'aria-label': title },
        h('strong', { className: 'msm-muted' }, title),
        selected.length ? h(State, { query }, ({ series }) => Object.values(series).some((samples) => samples.length) ? h(Chart, {
          rows, colors, label, detail: details, unit: 'ms', unitOnAxis: true, hoverStep, onHoverStep: setHoverStep,
          series: selected.map((name, index) => ({
            label: name,
            color: index < PALETTE.length ? PALETTE[(index + colorOffset) % PALETTE.length]
              : `hsl(${Math.round((index + colorOffset) * 137.508) % 360}, 65%, 60%)`,
            get: (row) => row[field].get(name)?.ms,
          })),
        }) : h(Hint, null, '所选范围内没有耗时样本。'))
          : h(Hint, null, names.length ? '请选择算子类型。' : '所选范围内没有可用算子类型。'));
    };

    return h(React.Fragment, null,
      h(Controls, null,
        h(Select, {
          label: 'Rank', value: chosenRank, change: setRank, disabled: !ranks.length,
          options: [['', '全部 Rank（中位数）'], ...ranks.map((value) => [value, `Rank ${value}`])],
        }),
        h(OperatorTypeSelect, { label: '计算算子类型', names: kernel.names, value: kernel.selected, change: kernel.change }),
        h(OperatorTypeSelect, { label: '通信算子类型', names: communication.names, value: communication.selected, change: communication.change })),
      h(State, { query: typesQuery }, () => h('div', { className: 'msm-operator-trends' },
        chart('kernel', '计算算子耗时', kernel, 0),
        chart('communication', '通信算子耗时', communication, 2))));
  }

  function StepPicker({ step }) {
    const current = step === '$step' ? '' : step;
    const [draft, setDraft] = React.useState(current);
    React.useEffect(() => setDraft(current), [current]);
    const valid = /^[0-9]+$/.test(draft) && Number.isSafeInteger(Number(draft));
    return h('form', {
      className: 'msm-controls',
      onSubmit: (event) => {
        event.preventDefault();
        if (valid && draft !== current) locationService.partial({ 'var-step': draft }, true);
      },
    },
    h(Input, { label: '明细 Step', value: draft, change: setDraft, type: 'number', width: 110 }),
    h('button', { type: 'submit', disabled: !valid || draft === current }, '应用'));
  }

  function Panel({ options, timeRange, replaceVariables, eventBus }) {
    const theme = useTheme2();
    const [refresh, setRefresh] = React.useState(0);
    React.useEffect(() => {
      const subscription = eventBus.getStream(RefreshEvent).subscribe(() => setRefresh((value) => value + 1));
      return () => subscription.unsubscribe();
    }, [eventBus]);
    const style = {
      '--msm-text': theme.colors.text.primary,
      '--msm-muted': theme.colors.text.secondary,
      '--msm-border': theme.colors.border.medium,
      '--msm-surface': theme.colors.background.secondary,
      '--msm-input': theme.colors.background.primary,
      '--msm-accent': theme.colors.primary.main,
    };
    const colors = { muted: style['--msm-muted'], border: style['--msm-border'] };
    const interpolate = (name) => replaceVariables ? replaceVariables(name) : getTemplateSrv().replace(name);
    const scope = {
      job: interpolate('$job'),
      run: interpolate('$run'),
      from: timeRange.from.valueOf(),
      to: timeRange.to.valueOf(),
      refresh,
    };
    const mode = options.mode;
    const l3 = ['tail', 'offset', 'top', 'communication', 'kde', 'kde-kernel', 'kde-communication'].includes(mode);
    const step = l3 || mode === 'step-picker' ? interpolate('$step') : '';
    let content;
    if (mode === 'step-picker') {
      content = h(StepPicker, { step });
    } else if (!scope.job || !scope.run || scope.job === '$job' || scope.run === '$run') {
      content = h(Hint, null, '请选择 job_name 和 run_id。');
    } else if (l3 && !/^[0-9]+$/.test(step)) {
      content = h(Hint, null, '请在趋势图下方输入明细 Step。');
    } else {
      const components = {
        recent: Recent, single: SingleRank, l2: L2, ranges: L2, coverage: L2, tail: DomainTable, offset: DomainTable,
        top: TopRanks, communication: CommunicationRanks, kde: Kde, 'kde-kernel': Kde, 'kde-communication': Kde,
        'operator-trends': OperatorTrends,
      };
      content = h(components[mode] || L1Overview, { scope: l3 ? { ...scope, step } : scope, mode, colors });
    }
    return h('div', { className: 'msm-panel', style }, content);
  }

  return {
    setters: [
      (react) => { React = react.default || react; },
      (data) => { ({ PanelPlugin } = data); },
      (runtime) => { ({ RefreshEvent, config, getBackendSrv, getTemplateSrv, locationService } = runtime); },
      (ui) => { ({ MultiCombobox, useTheme2 } = ui); },
    ],
    execute() {
      h = React.createElement;
      const style = document.createElement('link');
      style.rel = 'stylesheet';
      const version = config.panels?.['msmonitor-step-panel']?.info?.version;
      style.href = (config.appSubUrl || '') + '/public/plugins/msmonitor-step-panel/panel.css' + (version ? `?v=${encodeURIComponent(version)}` : '');
      document.head.appendChild(style);
      exports('plugin', new PanelPlugin(Panel).setPanelOptions((builder) => builder.addSelect({
        path: 'mode', name: '面板内容', defaultValue: 'mean',
        settings: {
          options: [
            ['mean', '各 Rank 平均耗时'], ['spread', '快慢卡耗时差'], ['missing', '缺报 Rank 数'],
            ['recent', '最近 Step 上报'], ['single', '单 Rank 趋势'], ['ranges', '用户自定义打点'], ['coverage', '通算掩盖'],
            ['tail', '域内耗时差异'], ['offset', '域整体耗时偏移'], ['top', 'Rank Top N'],
            ['communication', '单卡通信算子累计耗时'],
            ['kde-kernel', '计算算子 KDE 分布'], ['kde-communication', '通信算子 KDE 分布'],
            ['operator-trends', '算子累计耗时趋势'], ['step-picker', '明细 Step 筛选'],
          ].map(([value, label]) => ({ value, label })),
        },
      })));
    },
  };
});

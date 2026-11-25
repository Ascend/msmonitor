"""Unit tests for center tenant configuration and Kubernetes migration ordering."""

import json
import re
import subprocess  # nosec B404  # Simulate kubectl's migration error.
import sys
import tempfile
import unittest
from contextlib import ExitStack
from pathlib import Path
from unittest.mock import patch

with patch.object(sys, "path", [str(Path(__file__).resolve().parents[3] / "deploy/center/control"), *sys.path]):
    import center_config
    import install_k8s


class CenterDeploymentTest(unittest.TestCase):
    @patch.object(install_k8s.time, "sleep")
    def test_wait_ondelete_requires_ready_replicas_at_current_revision(self, sleep):
        obj = {
            "metadata": {"generation": 2},
            "spec": {"replicas": 2, "updateStrategy": {"type": "OnDelete"}},
            "status": {"observedGeneration": 2, "replicas": 2, "readyReplicas": 2, "updatedReplicas": 1},
        }
        first = json.dumps(obj)
        obj["status"]["updatedReplicas"] = 2
        snapshots = iter([first, json.dumps(obj)])
        install_k8s.wait_statefulset(lambda *args, **kwargs: next(snapshots), "statefulset/example")
        sleep.assert_called_once_with(2)

    def setUp(self):
        stack = ExitStack()
        self.addCleanup(stack.close)
        self.directory = Path(stack.enter_context(tempfile.TemporaryDirectory()))
        self.path = self.directory / "center.json"
        self.config = {
            "tenants": {
                "10": {"write_token": "a" * 32, "read_token": "b" * 32},
                "20": {"write_token": "c" * 32, "read_token": "d" * 32},
            },
            "i8_tokens": {"demo-job": "e" * 32},
            "cluster": {
                "namespace": "msmonitor-system",
                "image": "registry.example.com/msmonitor-center:v0.1",
                "host": "monitor.example.com",
                "ingress_class": "nginx",
                "gateway_namespace": "ingress-system",
                "gateway_pod_labels": {"gateway": "center"},
                "tls_secret": "center-tls",
                "database_url_file": "runtime-dsn",
                "migration_database_url_file": "migration-dsn",
            },
        }
        self.path.write_text(json.dumps(self.config), encoding="utf-8")
        for filename in ("runtime-dsn", "migration-dsn"):
            (self.directory / filename).write_text("dbname=example", encoding="utf-8")

    def test_auth_routes_keep_tenant_and_read_write_permissions_separate(self):
        loaded = center_config.load_config(self.path)
        auth = center_config.auth_config(loaded["tenants"], "http://writes", "http://reads")
        users = {user["name"]: user["url_map"][0] for user in json.loads(auth)["users"]}
        self.assertEqual(
            users["tenant-10-write"],
            {
                "url_prefix": "http://writes",
                "src_paths": ["/insert/10/prometheus/api/v1/write"],
            },
        )
        self.assertEqual(
            users["tenant-20-read"],
            {
                "url_prefix": "http://reads",
                "src_paths": [f"/select/20/prometheus/api/v1/{name}" for name in center_config.READ_ENDPOINTS],
            },
        )

    def test_tenant_zero_grafana_read_routes(self):
        tenants = {"0": self.config["tenants"]["10"]}
        users = json.loads(center_config.auth_config(tenants, "http://writes", "http://reads"))["users"]
        routes = {u["name"]: u["url_map"][0]["src_paths"] for u in users}
        prefix = "/select/0/prometheus/api/v1/"
        for suffix in ("status/buildinfo", "label/rank/values", "label/comm_group/values", "label/range_name/values"):
            self.assertTrue(any(re.fullmatch(p, prefix + suffix) for p in routes["tenant-0-read"]))
            self.assertFalse(any(re.fullmatch(p, prefix + suffix) for p in routes["tenant-0-write"]))
        for path in (
            prefix + "label/a/b/values",
            prefix + "status/config",
            "/select/1/prometheus/api/v1/label/rank/values",
        ):
            self.assertFalse(any(re.fullmatch(p, path) for p in routes["tenant-0-read"]))

    def test_unknown_configuration_keys_report_the_path(self):
        for key in ("retention_peroid", "scope_acounts"):
            self.path.write_text(json.dumps({**self.config, key: {}}), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, key):
                center_config.load_config(self.path)
        self.config["cluster"]["storage_clas"] = "fast"
        with self.assertRaisesRegex(ValueError, r"cluster\.storage_clas"):
            install_k8s.cluster_config(self.config)

    @patch.object(install_k8s.time, "sleep")
    def test_old_ready_storage_does_not_finish_retention_rollout(self, sleep):
        container = {"name": "vmstorage", "args": ["-retentionPeriod=1d"]}
        obj = {
            "metadata": {"generation": 1},
            "spec": {"replicas": 1, "template": {"spec": {"containers": [container]}}},
            "status": {"observedGeneration": 1, "replicas": 1, "readyReplicas": 1, "updatedReplicas": 1},
        }
        old = json.dumps(obj)
        container["args"] = ["-retentionPeriod=7d"]
        responses = iter([old, json.dumps(obj)])
        install_k8s.wait_statefulset(lambda *a, **kw: next(responses), "storage", retention="7d")
        sleep.assert_called_once_with(2)

    def test_registration_discovers_only_the_bound_tenant(self):
        self.assertEqual(center_config.registration_urls(self.config), {})
        self.config.update(public_write_origin="https://center.example:8443", scope_accounts={"demo-job": "20"})
        self.assertEqual(
            center_config.registration_urls(self.config),
            {"demo-job": "https://center.example:8443/insert/20/prometheus/api/v1/write"},
        )
        self.config["scope_accounts"]["demo-job"] = "30"
        with self.assertRaises(ValueError):
            center_config.registration_urls(self.config)

    def test_shared_token_across_roles_is_rejected(self):
        self.config["i8_tokens"]["demo-job"] = "a" * 32
        self.path.write_text(json.dumps(self.config), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "distinct"):
            center_config.load_config(self.path)

    def test_overlay_routes_cluster_and_uses_same_image_for_migration(self):
        output = self.directory / "overlay"
        output.mkdir()
        cluster = self.config["cluster"]
        migration = install_k8s.prepare_overlay(output, self.path, self.config, cluster)
        self.assertIn(cluster["image"], migration)
        ingress = json.loads((output / "ingress.json").read_text(encoding="utf-8"))
        routes = ingress["spec"]["rules"][0]["http"]["paths"]
        service_by_path = {route["path"]: route["backend"]["service"]["name"] for route in routes}
        self.assertEqual(
            service_by_path,
            {
                "/insert": "msmonitor-i3-auth",
                "/select": "msmonitor-i3-auth",
                "/api/v1/daemon/register": "msmonitor-i8",
                "/api/v1/daemon/heartbeat": "msmonitor-i8",
                "/api/v1/daemon/poll-commands": "msmonitor-i8",
                "/api/v1/daemon/ack": "msmonitor-i8",
                "/api/v1/daemon/submit-summary": "msmonitor-i8",
            },
        )
        overlay = json.loads((output / "kustomization.yaml").read_text(encoding="utf-8"))
        rule = next(item for item in overlay["patches"] if item["target"]["kind"] == "NetworkPolicy")
        self.assertEqual(json.loads(rule["patch"])[0]["path"], "/spec/ingress/0/from")
        credentials = json.loads((output / "secrets/migration/kustomization.yaml").read_text(encoding="utf-8"))
        self.assertEqual(credentials["secretGenerator"][0]["files"], ["database_url", "runtime_database_url"])

    def test_external_auth_and_storage_memory_overlay(self):
        cluster = self.config["cluster"]
        cluster.update(external_vmauth_secret=True, vmstorage_memory={"requests": "8Gi", "limits": "16Gi"})
        output = self.directory / "overlay"
        output.mkdir()
        install_k8s.prepare_overlay(output, self.path, self.config, install_k8s.cluster_config(self.config))
        secrets = json.loads((output / "secrets/kustomization.yaml").read_text())
        self.assertEqual([g["name"] for g in secrets["secretGenerator"]], ["msmonitor-i8"])
        overlay = json.loads((output / "kustomization.yaml").read_text())
        ops = json.loads(next(p["patch"] for p in overlay["patches"] if p["target"]["kind"] == "VMCluster"))
        self.assertEqual(
            {op["path"]: op["value"] for op in ops if op["path"].endswith("/memory")},
            {"/spec/vmstorage/resources/requests/memory": "8Gi", "/spec/vmstorage/resources/limits/memory": "16Gi"},
        )

    @patch.object(install_k8s, "command_for")
    def test_missing_external_auth_stops_before_apply(self, command_for):
        self.config["cluster"]["external_vmauth_secret"] = True
        self.path.write_text(json.dumps(self.config), encoding="utf-8")
        command_for.return_value.return_value = ""
        with self.assertRaisesRegex(ValueError, "auth.yaml"):
            install_k8s.install(self.path, "test-context", check_only=True)
        self.assertFalse(any(args[0] == "apply" for args, _ in command_for.return_value.call_args_list))

    def test_large_scale_capacity_is_opt_in(self):
        for enabled in (False, True):
            output = self.directory / str(enabled)
            output.mkdir()
            if enabled:
                self.config["cluster"]["i8_large_scale"] = True
            cluster = install_k8s.cluster_config(self.config)
            install_k8s.prepare_overlay(output, self.path, self.config, cluster)
            overlay = json.loads((output / "kustomization.yaml").read_text())
            ops = json.loads(next(p["patch"] for p in overlay["patches"] if p["target"]["name"] == "msmonitor-i8"))
            args = next((op["value"] for op in ops if op["path"].endswith("/args")), [])
            sizes = [
                op["value"]["value"]
                for op in ops
                if op["path"].endswith("/env/-") and op["value"]["name"] == "I8_DB_POOL_SIZE"
            ]
            self.assertEqual(sizes, ["16"] if enabled else [])
            capacities = {
                op["path"]: op["value"]
                for op in ops
                if op["path"] == "/spec/replicas" or op["path"].endswith("/limits/cpu")
            }
            self.assertEqual(
                capacities,
                {
                    "/spec/replicas": 4,
                    "/spec/template/spec/containers/0/resources/limits/cpu": "2",
                }
                if enabled
                else {},
            )
            if enabled:
                self.assertEqual(args[args.index("--limit-concurrency") + 1], "1024")
            else:
                self.assertEqual(args, [])
        self.config["cluster"]["i8_large_scale"] = "false"
        with self.assertRaisesRegex(ValueError, "boolean"):
            install_k8s.cluster_config(self.config)

    @staticmethod
    def rendered_i3(domain="cluster.local", slots=4):
        storage = {"storage": {"volumeClaimTemplate": {"spec": {"storageClassName": "fast"}}}}
        vm = {"vmstorage": storage, "vminsert": {"replicaCount": 3}, "clusterDomainName": domain}
        proxy = {"env": [{"name": "CLUSTER_DOMAIN", "value": "cluster.local"}]}
        source = {"namespaceSelector": {"matchLabels": {"kubernetes.io/metadata.name": "edge"}}}
        items = [
            {"kind": "VMCluster", "metadata": {"name": "msmonitor"}, "spec": vm},
            {
                "kind": "NetworkPolicy",
                "metadata": {"name": "msmonitor-i3-auth"},
                "spec": {"ingress": [{"from": [source]}]},
            },
            {
                "kind": "Deployment",
                "metadata": {"name": "msmonitor-i3-l4"},
                "spec": {"template": {"spec": {"containers": [proxy]}}},
            },
            {
                "kind": "ConfigMap",
                "metadata": {"name": "msmonitor-i3-l4"},
                "data": {"haproxy.cfg": f"server-template writer {slots} host"},
            },
        ]
        return json.dumps({"items": items})

    def preflight_response(self, *args, **_):
        if "--dry-run=server" in args and "-k" in args:
            return self.rendered_i3()
        if args[-1] == "jsonpath={.status.phase}":
            return "Active"
        return ""

    @patch.object(install_k8s, "command_for")
    def test_rendered_writer_configuration_blocks_install(self, command_for):
        command = command_for.return_value
        for kwargs, message in (({"domain": "other.local"}, "differ"), ({"slots": 3}, "slots")):
            with self.subTest(kwargs=kwargs):
                rendered = self.rendered_i3(**kwargs)
                command.side_effect = lambda *args, output=rendered, **opts: (
                    output if "-k" in args else self.preflight_response(*args, **opts)
                )
                with self.assertRaisesRegex(ValueError, message):
                    install_k8s.install(self.path, "test-context", check_only=False)
                command.assert_any_call("get", "storageclass", "fast", capture=True)
                self.assertFalse(any(args[0] == "create" for args, _ in command.call_args_list))

    @patch.object(install_k8s, "command_for")
    def test_missing_default_storage_class_blocks_preflight(self, command_for):
        resources = json.loads(self.rendered_i3())["items"]
        pvc = resources[0]["spec"]["vmstorage"]["storage"]["volumeClaimTemplate"]["spec"]
        del pvc["storageClassName"]

        def command(*args, **_):
            if "-k" in args:
                return "\n".join(json.dumps(item) for item in resources)
            if args[:2] == ("get", "storageclass"):
                return '{"items":[]}'
            return self.preflight_response(*args)

        command_for.return_value.side_effect = command
        with self.assertRaisesRegex(ValueError, "No default StorageClass"):
            install_k8s.install(self.path, "test-context", check_only=True)

    @patch.object(install_k8s, "command_for")
    def test_dry_run_never_creates_live_objects(self, command_for):
        command_for.return_value.side_effect = self.preflight_response
        install_k8s.install(self.path, "test-context", check_only=True)
        calls = [args for args, _ in command_for.return_value.call_args_list]
        self.assertTrue(any(args[0] == "apply" for args in calls))
        self.assertTrue(all("--dry-run=server" in args for args in calls if args[0] in ("apply", "create")))

    @patch.object(install_k8s, "command_for")
    def test_failed_migration_stops_before_workload_apply(self, command_for):
        def command(*args, **_):
            if args[0] == "create":
                return "job.batch/test-migration"
            if "--for=condition=complete" in args:
                raise subprocess.CalledProcessError(1, args)
            return self.preflight_response(*args)

        command_for.return_value.side_effect = command
        with self.assertRaises(subprocess.CalledProcessError):
            install_k8s.install(self.path, "test-context", check_only=False)
        calls = [args for args, _ in command_for.return_value.call_args_list]
        actual_applies = [args for args in calls if args[0] == "apply" and "--dry-run=server" not in args]
        self.assertEqual(len(actual_applies), 1)
        self.assertEqual(Path(actual_applies[0][-1]).name, "migration")


if __name__ == "__main__":
    unittest.main()

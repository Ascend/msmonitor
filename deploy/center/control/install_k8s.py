"""Install the center on Kubernetes using the shared image and the existing cluster manifests."""

import argparse
import hashlib
import json
import re
import shutil
import subprocess  # nosec B404  # Calls only the resolved kubectl executable.
import sys
import tempfile
import time
from pathlib import Path

from center_config import auth_config, check_keys, load_config, retention_period, registration_urls

CENTER = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(CENTER / "metrics"))
from preflight import check_rendered  # noqa: E402

DNS_LABEL = re.compile(r"[a-z0-9](?:[-a-z0-9]{0,61}[a-z0-9])?")
RELEASE_IMAGE = "msmonitor-i8:replace-with-release"


def dns_name(value, field, *, allow_dots=False):
    if not isinstance(value, str) or not value or len(value) > 253:
        raise ValueError(f"Set cluster.{field} to a Kubernetes DNS name")
    labels = value.split(".") if allow_dots else [value]
    if not all(DNS_LABEL.fullmatch(label) for label in labels):
        raise ValueError(f"Set cluster.{field} to a Kubernetes DNS name")
    return value


def cluster_config(config):
    cluster = config.get("cluster")
    if not isinstance(cluster, dict):
        raise ValueError("Set the cluster section of center.json")
    check_keys(
        cluster,
        (
            "namespace",
            "gateway_namespace",
            "host",
            "tls_secret",
            "ingress_class",
            "storage_class",
            "i8_large_scale",
            "external_vmauth_secret",
            "vmstorage_memory",
            "gateway_pod_labels",
            "image",
            "database_url_file",
            "migration_database_url_file",
        ),
        "cluster.",
    )
    for field in ("namespace", "gateway_namespace"):
        dns_name(cluster.get(field), field)
    for field in ("host", "tls_secret", "ingress_class"):
        dns_name(cluster.get(field), field, allow_dots=True)
    if cluster.get("storage_class") is not None:
        dns_name(cluster["storage_class"], "storage_class", allow_dots=True)
    if not isinstance(cluster.get("i8_large_scale", False), bool):
        raise ValueError("cluster.i8_large_scale must be a JSON boolean")
    if not isinstance(cluster.get("external_vmauth_secret", False), bool):
        raise ValueError("cluster.external_vmauth_secret must be a JSON boolean")
    if "vmstorage_memory" in cluster:
        memory = cluster["vmstorage_memory"]
        if isinstance(memory, dict):
            check_keys(memory, ("requests", "limits"), "cluster.vmstorage_memory.")
        if (
            not isinstance(memory, dict)
            or set(memory) != {"requests", "limits"}
            or not all(isinstance(v, str) and v.strip() for v in memory.values())
        ):
            raise ValueError("cluster.vmstorage_memory requires requests and limits as quantity strings")
    labels = cluster.get("gateway_pod_labels")
    if not isinstance(labels, dict) or not labels or not all(isinstance(value, str) for value in labels.values()):
        raise ValueError("Set cluster.gateway_pod_labels to the ingress controller's labels")
    image = cluster.get("image")
    if not isinstance(image, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._:/@-]{1,255}", image):
        raise ValueError("Set cluster.image to the published center image")
    if image.endswith(":release") or image == RELEASE_IMAGE:
        raise ValueError("Replace the example center image with its release tag or digest")
    return cluster


def json_file(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def kustomization(directory, **fields):
    json_file(
        directory / "kustomization.yaml",
        {
            "apiVersion": "kustomize.config.k8s.io/v1beta1",
            "kind": "Kustomization",
            **fields,
        },
    )


def credential_file(config_dir, cluster, key):
    relative = cluster.get(key)
    if not isinstance(relative, str) or not relative:
        raise ValueError(f"Set cluster.{key} to a PostgreSQL connection file")
    value = (config_dir / relative).read_bytes().strip()
    if not value or b"\n" in value or b"\r" in value or b"\0" in value:
        raise ValueError(f"cluster.{key} must contain one connection string")
    return value


def prepare_secrets(root, config_path, config, cluster):
    secrets = root / "secrets"
    secrets.mkdir()
    generators = [{"name": "msmonitor-i8", "files": ["database_url", "tokens.json"]}]
    if not cluster.get("external_vmauth_secret"):
        auth = auth_config(config["tenants"], "http://msmonitor-i3-l4:8480", "http://vmselect-msmonitor:8481")
        (secrets / "auth.yaml").write_text(auth, encoding="utf-8")
        generators.append({"name": "msmonitor-i3-auth", "files": ["auth.yaml"]})
    json_file(secrets / "tokens.json", config["i8_tokens"])
    runtime_dsn = credential_file(config_path.parent, cluster, "database_url_file")
    migration_dsn = credential_file(config_path.parent, cluster, "migration_database_url_file")
    (secrets / "database_url").write_bytes(runtime_dsn)
    migration = secrets / "migration"
    migration.mkdir()
    (migration / "database_url").write_bytes(migration_dsn)
    (migration / "runtime_database_url").write_bytes(runtime_dsn)
    kustomization(
        migration,
        generatorOptions={"disableNameSuffixHash": True},
        secretGenerator=[
            {"name": "msmonitor-i8-migration", "files": ["database_url", "runtime_database_url"]},
        ],
    )
    kustomization(
        secrets,
        resources=["migration"],
        generatorOptions={"disableNameSuffixHash": True},
        secretGenerator=generators,
    )
    return hashlib.sha256(runtime_dsn + b"\0" + (secrets / "tokens.json").read_bytes()).hexdigest()


def network_and_ingress(root, cluster):
    ingress_source = {
        "namespaceSelector": {"matchLabels": {"kubernetes.io/metadata.name": cluster["gateway_namespace"]}},
        "podSelector": {"matchLabels": cluster["gateway_pod_labels"]},
    }
    json_file(
        root / "i8-ingress-policy.json",
        {
            "apiVersion": "networking.k8s.io/v1",
            "kind": "NetworkPolicy",
            "metadata": {"name": "msmonitor-i8-ingress"},
            "spec": {
                "podSelector": {"matchLabels": {"app.kubernetes.io/name": "msmonitor-i8"}},
                "policyTypes": ["Ingress"],
                "ingress": [{"from": [ingress_source], "ports": [{"port": 8080, "protocol": "TCP"}]}],
            },
        },
    )
    paths = []
    for path, match, service, port in (
        ("/insert", "Prefix", "msmonitor-i3-auth", 8427),
        ("/select", "Prefix", "msmonitor-i3-auth", 8427),
        ("/api/v1/daemon/register", "Exact", "msmonitor-i8", 8080),
        ("/api/v1/daemon/heartbeat", "Exact", "msmonitor-i8", 8080),
        ("/api/v1/daemon/poll-commands", "Exact", "msmonitor-i8", 8080),
        ("/api/v1/daemon/ack", "Exact", "msmonitor-i8", 8080),
        ("/api/v1/daemon/submit-summary", "Exact", "msmonitor-i8", 8080),
    ):
        paths.append(
            {
                "path": path,
                "pathType": match,
                "backend": {"service": {"name": service, "port": {"number": port}}},
            }
        )
    json_file(
        root / "ingress.json",
        {
            "apiVersion": "networking.k8s.io/v1",
            "kind": "Ingress",
            "metadata": {"name": "msmonitor-center"},
            "spec": {
                "ingressClassName": cluster["ingress_class"],
                "tls": [{"hosts": [cluster["host"]], "secretName": cluster["tls_secret"]}],
                "rules": [{"host": cluster["host"], "http": {"paths": paths}}],
            },
        },
    )
    return ingress_source


def patch(kind, name, *changes):
    return {
        "target": {"kind": kind, "name": name},
        "patch": json.dumps([{"op": "add", "path": path, "value": value} for path, value in changes]),
    }


def prepare_overlay(root, config_path, config, cluster):
    runtime_revision = prepare_secrets(root, config_path, config, cluster)
    ingress_source = network_and_ingress(root, cluster)
    resources = ["secrets", "i8-ingress-policy.json", "ingress.json"]
    for path in ("metrics/vmcluster.yaml", "metrics/gateway.yaml", "control/k8s.yaml"):
        source = CENTER / path
        shutil.copyfile(source, root / source.name)
        resources.append(source.name)
    vm_changes = [("/spec/retentionPeriod", retention_period(config))]
    for field, quantity in cluster.get("vmstorage_memory", {}).items():
        vm_changes.append((f"/spec/vmstorage/resources/{field}/memory", quantity))
    i8_capacity = []
    if cluster.get("i8_large_scale"):
        i8_capacity = [
            ("/spec/replicas", 4),
            ("/spec/template/spec/containers/0/resources/limits/cpu", "2"),
            (
                "/spec/template/spec/containers/0/args",
                [
                    "uvicorn",
                    "app:app",
                    "--port",
                    "8080",
                    "--no-access-log",
                    "--no-proxy-headers",
                    "--ws",
                    "none",
                    "--limit-concurrency",
                    "1024",
                    "--timeout-keep-alive",
                    "5",
                    "--timeout-graceful-shutdown",
                    "15",
                ],
            ),
            ("/spec/template/spec/containers/0/env/-", {"name": "I8_DB_POOL_SIZE", "value": "16"}),
        ]
    if cluster.get("storage_class"):
        vm_changes.append(
            ("/spec/vmstorage/storage/volumeClaimTemplate/spec/storageClassName", cluster["storage_class"])
        )
    kustomization(
        root,
        namespace=cluster["namespace"],
        resources=resources,
        patches=[
            patch("VMCluster", "msmonitor", *vm_changes),
            patch("NetworkPolicy", "msmonitor-i3-auth", ("/spec/ingress/0/from", [ingress_source])),
            patch(
                "Deployment",
                "msmonitor-i8",
                ("/spec/template/spec/containers/0/image", cluster["image"]),
                (
                    "/spec/template/spec/containers/0/env/-",
                    {"name": "I8_WRITE_URLS", "value": json.dumps(registration_urls(config))},
                ),
                ("/spec/template/metadata/annotations", {"msmonitor.io/i8-config-revision": runtime_revision}),
                *i8_capacity,
            ),
        ],
    )
    migration = (CENTER / "control/k8s-migrate.yaml").read_text(encoding="utf-8")
    return migration.replace(RELEASE_IMAGE, json.dumps(cluster["image"]), 1)


def command_for(context, namespace):
    kubectl = shutil.which("kubectl")
    if kubectl is None:
        raise ValueError("kubectl is required on PATH")
    command = [str(Path(kubectl).resolve())]
    if context:
        command.extend(["--context", context])
    command.extend(["--namespace", namespace])

    def run(*arguments, capture=False, data=None):
        result = subprocess.run(  # nosec B603  # Resolved kubectl, argv only.
            [*command, *arguments],
            check=True,
            text=True,
            encoding="utf-8",
            input=data,
            stdout=subprocess.PIPE if capture else None,
            timeout=360,
        )
        return result.stdout.strip() if capture else None

    return run


def wait_statefulset(command, name, *, retention=None):
    deadline = time.monotonic() + 300
    while True:
        obj = json.loads(command("get", name, "-o", "json", capture=True))
        spec, status = obj["spec"], obj.get("status", {})
        desired = spec.get("replicas", 1)
        containers = spec.get("template", {}).get("spec", {}).get("containers", [])
        matches_retention = retention is None or any(
            c["name"] == "vmstorage" and f"-retentionPeriod={retention}" in c.get("args", []) for c in containers
        )
        if (
            matches_retention
            and status.get("observedGeneration") == obj["metadata"]["generation"]
            and status.get("replicas", 0) == desired
            and status.get("readyReplicas", 0) == desired
            and status.get("updatedReplicas", 0) == desired
        ):
            return
        if time.monotonic() >= deadline:
            raise ValueError(f"Timed out waiting for {name}: desired={desired}, status={status}")
        time.sleep(2)


def install(config_path, context, *, check_only):
    config = load_config(config_path)
    cluster = cluster_config(config)
    command = command_for(context, cluster["namespace"])
    with tempfile.TemporaryDirectory(prefix="msmonitor-center-") as directory:
        root = Path(directory)
        migration = prepare_overlay(root, config_path, config, cluster)
        command("get", "namespace", cluster["namespace"], "-o", "name")
        command("get", "namespace", cluster["gateway_namespace"], "-o", "name")
        command("get", "ingressclass", cluster["ingress_class"], "-o", "name")
        command("get", "secret", cluster["tls_secret"], "-o", "name")
        if cluster.get("external_vmauth_secret"):
            present = command(
                "get",
                "secret",
                "msmonitor-i3-auth",
                "-o",
                'go-template={{if index .data "auth.yaml"}}present{{end}}',
                capture=True,
            )
            if present != "present":
                raise ValueError("External Secret msmonitor-i3-auth requires a nonempty auth.yaml")
        command("wait", "--for=condition=Established", "--timeout=10s", "crd/vmclusters.operator.victoriametrics.com")
        apply = ("apply", "--server-side", "--field-manager=msmonitor-center")
        rendered = command(*apply, "--dry-run=server", "--validate=strict", "-k", directory, "-o", "json", capture=True)
        check_rendered(lambda *args: command(*args, capture=True), rendered)
        command("create", "--dry-run=server", "-f", "-", "-o", "name", data=migration)
        if check_only:
            print("PASS Kubernetes center preflight")
            return

        command(*apply, "-k", str(root / "secrets/migration"))
        job = command("create", "-f", "-", "-o", "name", data=migration, capture=True)
        command("wait", "--for=condition=complete", "--timeout=135s", job)
        command(*apply, "-k", directory)
        resource = "vmcluster.operator.victoriametrics.com/msmonitor"
        generation = command("get", resource, "-o", "jsonpath={.metadata.generation}", capture=True)
        command("wait", f"--for=jsonpath={{.status.observedGeneration}}={int(generation)}", "--timeout=300s", resource)
        command("wait", "--for=jsonpath={.status.updateStatus}=operational", "--timeout=300s", resource)
        for workload in (
            "deployment/msmonitor-i3-l4",
            "deployment/msmonitor-i3-auth",
            "deployment/msmonitor-i8",
            "deployment/vminsert-msmonitor",
        ):
            command("rollout", "status", workload, "--timeout=300s")
        wait_statefulset(command, "statefulset/vmselect-msmonitor")
        wait_statefulset(command, "statefulset/vmstorage-msmonitor", retention=retention_period(config))
    print(f"Center workloads are ready. HTTPS origin: https://{cluster['host']}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, required=True, help="Path to center.json")
    parser.add_argument("--context", help="Kubectl context to deploy to")
    parser.add_argument("--check", action="store_true", help="Validate with server dry-run only")
    args = parser.parse_args()
    try:
        install(args.config, args.context, check_only=args.check)
    except json.JSONDecodeError:
        parser.exit(2, "Invalid JSON in center configuration\n")
    except (ValueError, KeyError, OSError, subprocess.SubprocessError) as error:
        parser.exit(1, f"Center deployment failed: {error}\n")


if __name__ == "__main__":
    main()

"""Check an I3 Kubernetes deployment without persisting cluster resources."""

import argparse
import json
import math
import re
import subprocess  # nosec B404 - kubectl is invoked without a shell
from pathlib import Path
from shutil import which


HERE = Path(__file__).resolve().parent


def kubectl(*args):
    executable = which("kubectl")
    if executable is None:
        raise FileNotFoundError("kubectl is required on PATH")
    command = [str(Path(executable).resolve()), "--request-timeout=20s", *args]
    return subprocess.check_output(command, encoding="utf-8").strip()  # nosec B603 - argv, no shell


def check_namespace(command, name):
    phase = command("get", "namespace", name, "-o", "jsonpath={.status.phase}")
    if phase != "Active":
        raise ValueError(f"Namespace {name!r} is not Active")


def check_storage(command, cluster):
    pvc = cluster["spec"]["vmstorage"]["storage"]["volumeClaimTemplate"]["spec"]
    if "storageClassName" in pvc:
        storage_class = pvc["storageClassName"]
        if storage_class:
            command("get", "storageclass", storage_class)
            print(f"StorageClass: {storage_class}")
        else:
            print("StorageClass: none, validate the pre-provisioned PVs before rollout")
        return

    classes = json.loads(command("get", "storageclass", "-o", "json"))["items"]
    defaults = [
        item["metadata"]
        for item in classes
        if item["metadata"].get("annotations", {}).get("storageclass.kubernetes.io/is-default-class") == "true"
        or item["metadata"].get("annotations", {}).get("storageclass.beta.kubernetes.io/is-default-class") == "true"
    ]
    if not defaults:
        raise ValueError("No default StorageClass. Set vmstorage's PVC storageClassName.")
    defaults.sort(key=lambda item: item["name"])
    default = max(defaults, key=lambda item: item["creationTimestamp"])
    print(f"StorageClass: {default['name']} (default)")


def check_gateway(command, policy):
    gateways = set()
    for rule in policy["spec"]["ingress"]:
        if not rule.get("from"):
            raise ValueError("vmauth ingress rule must identify its gateway")
        for source in rule["from"]:
            namespace = source.get("namespaceSelector", {}).get("matchLabels", {}).get("kubernetes.io/metadata.name")
            if not namespace or namespace == "replace-with-gateway-namespace":
                raise ValueError("Set the trusted gateway namespace in gateway.yaml")
            gateways.add(namespace)
    if not gateways:
        raise ValueError("Set the trusted gateway ingress rule in gateway.yaml")
    for gateway in sorted(gateways):
        check_namespace(command, gateway)
        print(f"Trusted gateway namespace: {gateway}")


def check_writer_pool(cluster, deployment, config):
    domain = cluster["spec"]["clusterDomainName"]
    container = deployment["spec"]["template"]["spec"]["containers"][0]
    env = {item["name"]: item.get("value") for item in container.get("env", [])}
    if env.get("CLUSTER_DOMAIN") != domain:
        raise ValueError("VMCluster clusterDomainName and HAProxy CLUSTER_DOMAIN differ")

    insert = cluster["spec"]["vminsert"]
    replicas = insert["replicaCount"]
    max_surge = (
        0 if insert.get("updateStrategy") == "Recreate" else insert.get("rollingUpdate", {}).get("maxSurge", "25%")
    )
    surge = (
        math.ceil(replicas * int(max_surge[:-1]) / 100)
        if isinstance(max_surge, str) and max_surge.endswith("%")
        else int(max_surge)
    )
    slots = re.search(r"(?m)^\s*server-template\s+writer\s+(\d+)\s", config["data"]["haproxy.cfg"])
    if slots is None or int(slots.group(1)) < replicas + surge:
        raise ValueError(f"HAProxy writer slots must cover {replicas} replicas plus {surge} rollout Pods")
    print(f"Writer pool: {replicas} replicas, {surge} rollout Pods, {slots.group(1)} slots")


def run(namespace, context):
    def command(*args):
        return kubectl("--context", context, *args)

    print(f"Context: {context}, namespace: {namespace}", flush=True)
    check_namespace(command, namespace)
    command("wait", "--for=condition=Established", "--timeout=10s", "crd/vmclusters.operator.victoriametrics.com")
    rendered = command(
        "-n",
        namespace,
        "apply",
        "--dry-run=server",
        "--validate=strict",
        "-o",
        "json",
        "-f",
        str(HERE / "vmcluster.yaml"),
        "-f",
        str(HERE / "gateway.yaml"),
    )
    check_rendered(command, rendered)
    print("PASS I3 Kubernetes preflight (server dry-run)")


def check_rendered(command, rendered):
    """Check the objects returned by kubectl for this deployment."""
    resources = {}
    decoder = json.JSONDecoder()
    pos = 0
    while pos < len(rendered):
        obj, pos = decoder.raw_decode(rendered, pos)
        for item in obj.get("items", [obj]):
            resources[item["kind"], item["metadata"]["name"]] = item
        while pos < len(rendered) and rendered[pos].isspace():
            pos += 1
    cluster = resources["VMCluster", "msmonitor"]
    check_storage(command, cluster)
    check_gateway(command, resources["NetworkPolicy", "msmonitor-i3-auth"])
    check_writer_pool(cluster, resources["Deployment", "msmonitor-i3-l4"], resources["ConfigMap", "msmonitor-i3-l4"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-n", "--namespace", required=True)
    parser.add_argument("--context", help="kubectl context, defaults to the current context")
    args = parser.parse_args()
    try:
        run(args.namespace, args.context or kubectl("config", "current-context"))
    except (OSError, subprocess.CalledProcessError, ValueError, KeyError) as error:
        parser.exit(1, f"I3 preflight failed: {error}\n")


if __name__ == "__main__":
    main()

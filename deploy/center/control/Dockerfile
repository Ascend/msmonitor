ARG VM_VERSION=v1.155.0
FROM victoriametrics/vmstorage:${VM_VERSION}-cluster AS vmstorage
FROM victoriametrics/vminsert:${VM_VERSION}-cluster AS vminsert
FROM victoriametrics/vmselect:${VM_VERSION}-cluster AS vmselect
FROM victoriametrics/vmauth:${VM_VERSION} AS vmauth

FROM python:3.12-slim-bookworm AS i8-deps
WORKDIR /build-context
COPY . .
RUN if [ -d wheelhouse ]; then \
        python -m pip install --prefix=/install --ignore-installed --disable-pip-version-check --no-cache-dir --only-binary=:all: --no-index \
            --find-links=wheelhouse -r requirements.txt; \
    else \
        python -m pip install --prefix=/install --ignore-installed --no-cache-dir --only-binary=:all: \
            --timeout 60 --retries 8 -r requirements.txt; \
    fi

FROM python:3.12-slim-bookworm AS i8-service
ENV PYTHONUNBUFFERED=1 PYTHONDONTWRITEBYTECODE=1 UVICORN_HOST=0.0.0.0
WORKDIR /app
COPY --from=i8-deps /install/ /usr/local/
COPY app.py registry.py ./
USER 10001:10001
EXPOSE 8080
CMD ["uvicorn", "app:app", "--port", "8080", "--no-access-log", "--no-proxy-headers", "--ws", "none", "--limit-concurrency", "256", "--timeout-keep-alive", "5", "--timeout-graceful-shutdown", "15"]

FROM python:3.12-slim-bookworm AS center

ARG DEBIAN_MIRROR_URL=https://deb.debian.org
RUN sed -i -E "s|https?://deb.debian.org/|${DEBIAN_MIRROR_URL}/|g" /etc/apt/sources.list.d/debian.sources \
    && apt-get update \
    && apt-get install -y --no-install-recommends postgresql-common \
    && printf '\ncreate_main_cluster = false\n' >> /etc/postgresql-common/createcluster.conf \
    && apt-get install -y --no-install-recommends postgresql-15 haproxy tini \
    && groupadd --gid 10001 msmonitor \
    && useradd --uid 10001 --gid 10001 --no-create-home msmonitor \
    && install -d -m 0700 -o 10001 -g 10001 /data \
    && install -m 0600 -o 10001 -g 10001 /dev/null /data/.center.lock \
    && rm -rf /var/lib/apt/lists/*

COPY --from=vmstorage /vmstorage-prod /usr/local/bin/vmstorage
COPY --from=vminsert /vminsert-prod /usr/local/bin/vminsert
COPY --from=vmselect /vmselect-prod /usr/local/bin/vmselect
COPY --from=vmauth /vmauth-prod /usr/local/bin/vmauth
ENV PYTHONUNBUFFERED=1 PYTHONDONTWRITEBYTECODE=1 UVICORN_HOST=0.0.0.0
WORKDIR /app
COPY --from=i8-deps /install/ /usr/local/
COPY app.py registry.py center.py center_config.py standalone-haproxy.cfg ./

USER 10001:10001
EXPOSE 8443
HEALTHCHECK --interval=10s --timeout=15s --start-period=120s CMD ["python", "/app/center.py", "healthcheck"]
ENTRYPOINT ["/usr/bin/tini", "--", "python", "/app/center.py"]
CMD ["uvicorn", "app:app", "--port", "8080", "--no-access-log", "--no-proxy-headers", "--ws", "none", "--limit-concurrency", "256", "--timeout-keep-alive", "5", "--timeout-graceful-shutdown", "15"]

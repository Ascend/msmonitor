FROM grafana/grafana:12.2.0

USER root
RUN apk add --no-cache python3
COPY backend /opt/msmonitor
COPY entrypoint.sh /opt/msmonitor/entrypoint.sh
COPY provisioning /etc/grafana/provisioning
COPY dashboards /etc/grafana/dashboards
COPY --chown=472:0 plugins /opt/grafana-plugins
ENV GF_PATHS_PLUGINS=/opt/grafana-plugins
USER 472

ENTRYPOINT ["/bin/bash", "/opt/msmonitor/entrypoint.sh"]

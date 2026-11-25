ARG CENTER_IMAGE=msmonitor-center:base
ARG GRAFANA_IMAGE=grafana/grafana:12.2.0-ubuntu
FROM ${GRAFANA_IMAGE} AS grafana
FROM ${CENTER_IMAGE}

COPY --from=grafana /usr/share/grafana /usr/share/grafana
COPY grafana/backend /opt/msmonitor-visualization/backend
COPY grafana/provisioning /opt/msmonitor-visualization/provisioning
COPY grafana/dashboards /etc/grafana/dashboards
COPY grafana/plugins /opt/msmonitor-visualization/plugins

RUN /usr/share/grafana/bin/grafana --version
ENV MSMONITOR_VISUALIZATION=1
EXPOSE 3000

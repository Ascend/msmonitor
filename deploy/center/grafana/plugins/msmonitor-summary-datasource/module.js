System.register(['@grafana/data', '@grafana/runtime'], function (exports) {
  let DataSourceApi, DataSourcePlugin, getBackendSrv;

  return {
    setters: [
      (data) => { ({ DataSourceApi, DataSourcePlugin } = data); },
      (runtime) => { ({ getBackendSrv } = runtime); },
    ],
    execute() {
      class SummaryDataSource extends DataSourceApi {
        constructor(settings) {
          super(settings);
          this.url = settings.url;
        }

        async query() {
          return { data: [] };
        }

        async testDatasource() {
          await getBackendSrv().get(`${this.url}/summary/health`);
          return { status: 'success', message: 'Step 摘要服务可用。' };
        }
      }

      exports('plugin', new DataSourcePlugin(SummaryDataSource));
    },
  };
});

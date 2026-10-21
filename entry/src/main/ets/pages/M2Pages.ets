import { TftpdService, SyslogMsg, DnsQuery, ClientState, DirEntry } from '../services/TftpdService';

// TftpdService 的事件缓存在静态数组中，ArkUI 不会自动追踪：
// 各页签用 500ms 定时器把静态数组/对象快照到 @State，实现界面刷新。

@Component
export struct SyslogPage {
  @State msgs: SyslogMsg[] = [];
  private timer: number = -1;

  aboutToAppear(): void {
    this.msgs = TftpdService.syslogs.slice();
    this.timer = setInterval(() => {
      this.msgs = TftpdService.syslogs.slice();
    }, 500);
  }

  aboutToDisappear(): void {
    if (this.timer !== -1) clearInterval(this.timer);
  }

  build() {
    Column({ space: 8 }) {
      Row() {
        Text('Syslog 服务器').fontSize(20).fontWeight(FontWeight.Bold)
        Blank()
        Text('端口 ' + TftpdService.getStr('SyslogPort')).fontSize(14).fontColor('#888')
      }.width('100%').padding(12)
      List({ space: 4 }) {
        ForEach(this.msgs, (m: SyslogMsg) => {
          ListItem() {
            Column() {
              Text(m.from).fontSize(12).fontColor('#0A59F7')
              Text(m.txt).fontSize(14)
            }.alignItems(HorizontalAlign.Start).width('100%')
          }
        }, (m: SyslogMsg, i: number) => `${i}_${m.from}_${m.txt}`)
      }.layoutWeight(1).width('100%').padding({ left: 12, right: 12 })
    }.width('100%').height('100%')
  }
}

@Component
export struct SntpPage {
  build() {
    Column({ space: 12 }) {
      Text('SNTP 服务器').fontSize(20).fontWeight(FontWeight.Bold)
      Text('端口 ' + TftpdService.getStr('SntpPort')).fontSize(14).fontColor('#888')
      Text('服务开关位于「设置」页签。客户端发送 NTP v3 请求（mode=3）即可获得应答。')
        .fontSize(14).fontColor('#666')
    }.width('100%').height('100%').padding(24).alignItems(HorizontalAlign.Start)
  }
}

@Component
export struct DnsPage {
  @State hosts: string = '';
  @State queries: DnsQuery[] = [];
  @State status: string = '';
  private timer: number = -1;

  aboutToAppear(): void {
    this.hosts = TftpdService.readText(TftpdService.baseDir + '/hosts.txt');
    this.queries = TftpdService.dnsQueries.slice();
    this.timer = setInterval(() => {
      this.queries = TftpdService.dnsQueries.slice();
    }, 500);
  }

  aboutToDisappear(): void {
    if (this.timer !== -1) clearInterval(this.timer);
  }

  build() {
    Column({ space: 8 }) {
      Row() {
        Text('DNS 服务器').fontSize(20).fontWeight(FontWeight.Bold)
        Blank()
        Text('端口 ' + TftpdService.getStr('DnsPort')).fontSize(14).fontColor('#888')
      }.width('100%')
      Text('hosts.txt（每行：主机名 IPv4 IPv6，缺省用 -）').fontSize(12).fontColor('#666')
      TextArea({ text: this.hosts })
        .width('100%').height(140)
        .onChange((v: string) => { this.hosts = v; })
      Row({ space: 12 }) {
        Button('保存 hosts.txt').onClick(() => {
          const ok = TftpdService.writeText(TftpdService.baseDir + '/hosts.txt', this.hosts);
          this.status = ok ? '已保存' : ('保存失败 ' + TftpdService.lastError);
        })
        Text(this.status).fontSize(14).fontColor('#0A59F7')
      }
      Text('最近查询').fontSize(16).margin({ top: 12 })
      List({ space: 2 }) {
        ForEach(this.queries, (q: DnsQuery) => {
          ListItem() {
            Text(q.name + '  →  ' + (q.ipv4 !== '' ? q.ipv4 : q.ipv6))
              .fontSize(14).width('100%')
          }
        }, (q: DnsQuery, i: number) => `${i}_${q.name}`)
      }.layoutWeight(1).width('100%')
    }.width('100%').height('100%').padding(12)
  }
}

@Component
export struct ClientPage {
  @State host: string = '127.0.0.1';
  @State port: string = TftpdService.getInt('Port') > 0 ? TftpdService.getInt('Port').toString() : '6900';
  @State remote: string = '';
  @State local: string = '';
  @State blkSize: string = '0';
  @State st: ClientState = { state: 'idle', bytes: 0, size: 0, blk: 0, rc: 0, msg: '', md5: '' };
  private timer: number = -1;

  aboutToAppear(): void {
    this.local = TftpdService.baseDir + '/client_dl.bin';
    this.loadForm();
    this.syncState();
    this.timer = setInterval(() => {
      this.syncState();
    }, 500);
  }

  aboutToDisappear(): void {
    if (this.timer !== -1) clearInterval(this.timer);
  }

  private syncState(): void {
    const c = TftpdService.client;
    const s: ClientState = {
      state: c.state, bytes: c.bytes, size: c.size,
      blk: c.blk, rc: c.rc, msg: c.msg, md5: c.md5
    };
    this.st = s;
  }

  private busy(): boolean {
    return this.st.state === 'start' || this.st.state === 'progress';
  }

  /* 记住上一次客户端参数（host|port|remote|local|blkSize），下次进入自动回填、
   * 便于重复传输时免去重输；文件缺失时保持页面默认值。 */
  private loadForm(): void {
    const txt = TftpdService.readText(TftpdService.baseDir + '/client_form.txt');
    if (txt === '') return;
    const f = txt.split('|');
    if (f.length < 5) return;
    this.host = f[0];
    this.port = f[1];
    this.remote = f[2];
    if (f[3] !== '') this.local = f[3];
    this.blkSize = f[4];
  }

  private saveForm(): void {
    TftpdService.writeText(TftpdService.baseDir + '/client_form.txt',
      [this.host, this.port, this.remote, this.local, this.blkSize].join('|'));
  }

  build() {
    Column({ space: 10 }) {
      Text('TFTP 客户端').fontSize(20).fontWeight(FontWeight.Bold)
      Row({ space: 8 }) {
        Text('服务器').width(90)
        TextInput({ text: this.host }).layoutWeight(2).onChange((v: string) => { this.host = v; })
        Text('端口').width(48)
        TextInput({ text: this.port }).width(90).onChange((v: string) => { this.port = v; })
      }.width('100%')
      Row({ space: 8 }) {
        Text('远端文件').width(90)
        TextInput({ text: this.remote }).layoutWeight(1).onChange((v: string) => { this.remote = v; })
      }.width('100%')
      Row({ space: 8 }) {
        Text('本地文件').width(90)
        TextInput({ text: this.local }).layoutWeight(1).onChange((v: string) => { this.local = v; })
      }.width('100%')
      Row({ space: 8 }) {
        Text('块大小').width(90)
        TextInput({ text: this.blkSize }).width(120).onChange((v: string) => { this.blkSize = v; })
        Text('0 = 默认 512').fontSize(12).fontColor('#888')
      }.width('100%')
      Row({ space: 16 }) {
        Button('下载 (GET)').onClick(() => {
          this.saveForm();
          TftpdService.startClient(1, this.host, Number(this.port),
            this.remote, this.local, Number(this.blkSize));
        }).enabled(!this.busy())
        Button('上传 (PUT)').onClick(() => {
          this.saveForm();
          TftpdService.startClient(2, this.host, Number(this.port),
            this.remote, this.local, Number(this.blkSize));
        }).enabled(!this.busy())
      }
      Text('状态：' + this.st.state + '  块 ' + this.st.blk + '  ' +
        this.st.bytes + '/' + this.st.size + '  错误码 ' + this.st.rc)
        .fontSize(14)
      Text('MD5：' + this.st.md5).fontSize(14).fontColor('#0A59F7')
      Text(this.st.msg).fontSize(14).fontColor('#D81E06')
    }.width('100%').height('100%').padding(16).alignItems(HorizontalAlign.Start)
  }
}

@Component
export struct DirectoryPage {
  @State files: DirEntry[] = [];
  @State md5Text: string = '';
  @State baseDir: string = '';

  aboutToAppear(): void {
    this.baseDir = TftpdService.baseDir;
    this.refresh();
  }

  private refresh(): void {
    this.files = TftpdService.listDir(this.baseDir);
  }

  build() {
    Column({ space: 8 }) {
      Row() {
        Text('目录：' + this.baseDir)
          .fontSize(16).fontWeight(FontWeight.Bold).layoutWeight(1)
          .maxLines(1).textOverflow({ overflow: TextOverflow.Ellipsis })
        Button('刷新').onClick(() => { this.refresh(); })
      }.width('100%')
      List({ space: 2 }) {
        ForEach(this.files, (f: DirEntry) => {
          ListItem() {
            Row() {
              Text((f.dir === 1 ? '[DIR] ' : '') + f.name).fontSize(14).layoutWeight(1)
                .maxLines(1).textOverflow({ overflow: TextOverflow.Ellipsis })
              Text(f.size.toString()).fontSize(12).fontColor('#888')
              Button('MD5').height(28).margin({ left: 12 }).onClick(() => {
                this.md5Text = f.name + ': ' + TftpdService.md5(this.baseDir + '/' + f.name);
              })
            }.width('100%')
          }
        }, (f: DirEntry, i: number) => `${i}_${f.name}_${f.size}`)
      }.layoutWeight(1).width('100%')
      Text(this.md5Text).fontSize(12).copyOption(CopyOptions.LocalDevice)
    }.width('100%').height('100%').padding(12)
  }
}

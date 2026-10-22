import { TftpdService, SyslogMsg, DnsQuery, ClientState, DirEntry, DhcpLease } from '../services/TftpdService';

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

/* DHCP 服务器页：参数表单 + 租约表（轮询 PalDhcpLeasesJson）
 * 键名与原版注册表一致；地址参数保存后按需重启 DHCP 服务使之生效 */
@Component
export struct DhcpPage {
  @State pool: string = '';
  @State poolSize: string = '100';
  @State netMask: string = '255.255.255.0';
  @State gateway: string = '';
  @State dns1: string = '';
  @State dns2: string = '';
  @State wins: string = '';
  @State bootFile: string = '';
  @State domain: string = '';
  @State lease: string = '2880';
  @State opt42: string = '';
  @State opt120: string = '';
  @State optNb: string = '0';      // AddOptionNumber1（原版自定义选项，0 关闭）
  @State optVal: string = '';      // AddOptionValue1
  @State port: string = '6700';
  @State persLeases: boolean = true;
  @State leases: DhcpLease[] = [];
  @State status: string = '';
  private timer: number = -1;

  aboutToAppear(): void {
    this.loadForm();
    this.leases = TftpdService.leases();
    this.timer = setInterval(() => {
      this.leases = TftpdService.leases();
    }, 1000);
  }

  aboutToDisappear(): void {
    if (this.timer !== -1) clearInterval(this.timer);
  }

  private loadForm(): void {
    this.pool = TftpdService.getStr('IP_Pool');
    const n = TftpdService.getInt('PoolSize');
    if (n >= 0) this.poolSize = n.toString();
    const m = TftpdService.getStr('Mask');
    if (m !== '') this.netMask = m;
    this.gateway = TftpdService.getStr('Gateway');
    this.dns1 = TftpdService.getStr('DNS');
    this.dns2 = TftpdService.getStr('DNS2');
    this.wins = TftpdService.getStr('WINS');
    this.bootFile = TftpdService.getStr('BootFile');
    this.domain = TftpdService.getStr('DomainName');
    const l = TftpdService.getInt('Lease (minutes)');
    if (l > 0) this.lease = l.toString();
    this.opt42 = TftpdService.getStr('Option42');
    this.opt120 = TftpdService.getStr('Option120');
    const nb = TftpdService.getInt('AddOptionNumber1');
    if (nb >= 0) this.optNb = nb.toString();
    this.optVal = TftpdService.getStr('AddOptionValue1');
    const p = TftpdService.getInt('DhcpPort');
    if (p > 0) this.port = p.toString();
    const pl = TftpdService.getInt('PersistantLeases');
    if (pl >= 0) this.persLeases = pl !== 0;
  }

  private dhcpRunning(): boolean {
    const svc = TftpdService.getInt('Services');
    return svc > 0 && (svc & TftpdService.SERVICE_DHCP) !== 0;
  }

  private apply(): void {
    TftpdService.setStr('IP_Pool', this.pool.trim());
    TftpdService.setInt('PoolSize', parseInt(this.poolSize) || 0);
    TftpdService.setStr('Mask', this.netMask.trim());
    TftpdService.setStr('Gateway', this.gateway.trim());
    TftpdService.setStr('DNS', this.dns1.trim());
    TftpdService.setStr('DNS2', this.dns2.trim());
    TftpdService.setStr('WINS', this.wins.trim());
    TftpdService.setStr('BootFile', this.bootFile.trim());
    TftpdService.setStr('DomainName', this.domain.trim());
    TftpdService.setInt('Lease (minutes)', parseInt(this.lease) || 2880);
    TftpdService.setStr('Option42', this.opt42.trim());
    TftpdService.setStr('Option120', this.opt120.trim());
    TftpdService.setInt('AddOptionNumber1', parseInt(this.optNb) || 0);
    TftpdService.setStr('AddOptionValue1', this.optVal.trim());
    TftpdService.setInt('DhcpPort', parseInt(this.port) || 6700);
    TftpdService.setInt('PersistantLeases', this.persLeases ? 1 : 0);
    const ok = TftpdService.save();
    // 服务在运行时重启 DHCP，使新地址池/端口立即生效（原版改配置后需重开服务）
    if (this.dhcpRunning()) {
      TftpdService.stopMask(TftpdService.SERVICE_DHCP);
      TftpdService.startMask(TftpdService.SERVICE_DHCP);
    }
    this.status = ok ? '已保存，DHCP 服务已按新配置重启' : '保存失败';
  }

  private stateText(s: string): string {
    if (s === 'ack') return '已确认';
    if (s === 'offer') return '已提议';
    if (s === 'expired') return '已过期';
    return '空闲';
  }

  private fmtTime(t: number): string {
    if (t <= 0) return '-';
    const d = new Date(t * 1000);
    const p2 = (v: number): string => v < 10 ? '0' + v : '' + v;
    return `${p2(d.getMonth() + 1)}-${p2(d.getDate())} ${p2(d.getHours())}:${p2(d.getMinutes())}`;
  }

  build() {
    /* 表单行数较多（手机一屏放不下），必须可滚动，否则租约表被裁掉 */
    Scroll() {
      Column({ space: 8 }) {
        Row() {
          Text('DHCP 服务器').fontSize(20).fontWeight(FontWeight.Bold)
          Blank()
          Text('端口 ' + this.port).fontSize(14).fontColor('#888')
        }.width('100%')
      Row({ space: 8 }) {
        Text('地址池起始').width(110)
        TextInput({ text: this.pool }).layoutWeight(1)
          .onChange((v: string) => { this.pool = v; })
        Text('数量').width(48)
        TextInput({ text: this.poolSize }).width(80).type(InputType.Number)
          .onChange((v: string) => { this.poolSize = v; })
      }.width('100%')
      Row({ space: 8 }) {
        Text('子网掩码').width(110)
        TextInput({ text: this.netMask }).layoutWeight(1)
          .onChange((v: string) => { this.netMask = v; })
      }.width('100%')
      Row({ space: 8 }) {
        Text('网关').width(110)
        TextInput({ text: this.gateway }).layoutWeight(1)
          .onChange((v: string) => { this.gateway = v; })
      }.width('100%')
      Row({ space: 8 }) {
        Text('DNS 1').width(110)
        TextInput({ text: this.dns1 }).layoutWeight(1)
          .onChange((v: string) => { this.dns1 = v; })
      }.width('100%')
      Row({ space: 8 }) {
        Text('DNS 2').width(110)
        TextInput({ text: this.dns2 }).layoutWeight(1)
          .onChange((v: string) => { this.dns2 = v; })
      }.width('100%')
      Row({ space: 8 }) {
        Text('WINS').width(110)
        TextInput({ text: this.wins }).layoutWeight(1)
          .onChange((v: string) => { this.wins = v; })
      }.width('100%')
      Row({ space: 8 }) {
        Text('启动文件').width(110)
        TextInput({ text: this.bootFile }).layoutWeight(1)
          .onChange((v: string) => { this.bootFile = v; })
      }.width('100%')
      Row({ space: 8 }) {
        Text('域名').width(110)
        TextInput({ text: this.domain }).layoutWeight(1)
          .onChange((v: string) => { this.domain = v; })
      }.width('100%')
      Row({ space: 8 }) {
        Text('租期(分钟)').width(110)
        TextInput({ text: this.lease }).width(100).type(InputType.Number)
          .onChange((v: string) => { this.lease = v; })
        Text('选项42').width(60)
        TextInput({ text: this.opt42 }).layoutWeight(1)
          .onChange((v: string) => { this.opt42 = v; })
      }.width('100%')
      Row({ space: 8 }) {
        Text('选项120').width(110)
        TextInput({ text: this.opt120 }).layoutWeight(1)
          .onChange((v: string) => { this.opt120 = v; })
      }.width('100%')
      Row({ space: 8 }) {
        Text('自定义选项').width(110)
        TextInput({ text: this.optNb }).width(80).type(InputType.Number)
          .onChange((v: string) => { this.optNb = v; })
        TextInput({ text: this.optVal, placeholder: '选项值' }).layoutWeight(1)
          .onChange((v: string) => { this.optVal = v; })
      }.width('100%')
      Text('自定义选项：左侧为选项编号（0 关闭），右侧为选项值；支持原版表达式如 192.168.1.%3、%s。' +
        '编号 66 同时用作 TFTP 服务器地址（siaddr）。')
        .fontSize(12).fontColor('#888')
      Row({ space: 8 }) {
        Text('服务端口').width(110)
        TextInput({ text: this.port }).width(100).type(InputType.Number)
          .onChange((v: string) => { this.port = v; })
        Text('持久租约').width(70)
        Toggle({ type: ToggleType.Switch, isOn: this.persLeases })
          .onChange((on: boolean) => { this.persLeases = on; })
      }.width('100%')
      Row({ space: 12 }) {
        Button('保存并应用').onClick(() => { this.apply(); })
        Text(this.status).fontSize(13).fontColor('#0A59F7').layoutWeight(1)
      }.width('100%')
      Text('提示：鸿蒙无 raw socket 权限，DHCP Ping/ARP 冲突检测不可用，地址直接分配。')
        .fontSize(12).fontColor('#888')
      Row() {
        Text('租约表（' + this.leases.length + '）').fontSize(16)
        Blank()
        Button('刷新').height(28).onClick(() => { this.leases = TftpdService.leases(); })
      }.width('100%').margin({ top: 4 })
      List({ space: 2 }) {
        ForEach(this.leases, (l: DhcpLease) => {
          ListItem() {
            Row() {
              Text(l.ip).fontSize(14).width(120)
              Text(l.mac).fontSize(12).width(140).fontColor('#666')
              Text(this.stateText(l.state)).fontSize(12).width(60)
              Text(this.fmtTime(l.renew > 0 ? l.renew : l.alloc)).fontSize(12).fontColor('#888')
            }.width('100%')
          }
        }, (l: DhcpLease, i: number) => `${i}_${l.ip}_${l.mac}_${l.state}_${l.renew}`)
      }.height(260).width('100%')
      }.width('100%').padding(12).alignItems(HorizontalAlign.Start)
    }.width('100%').height('100%').scrollBar(BarState.Auto)
  }
}

import { TftpdService, TrfStat } from '../services/TftpdService';
import { SyslogPage, SntpPage, DnsPage, ClientPage, DirectoryPage, DhcpPage } from './M2Pages';
import backgroundTaskManager from '@ohos.resourceschedule.backgroundTaskManager';
import { wantAgent, common } from '@kit.AbilityKit';
import { BusinessError } from '@kit.BasicServicesKit';
import { notificationManager } from '@kit.NotificationKit';

// 设置键名与原版 Tftpd32 注册表键完全一致（兼容性强原则）
const KEY_PORT = 'TftpPort';
const KEY_BASEDIR = 'BaseDirectory';
const KEY_SECURITY = 'SecurityLevel';
const KEY_TIMEOUT = 'Timeout';
const KEY_RETRANSMIT = 'MaxRetransmit';
const KEY_NEGOCIATE = 'Negociate';
const KEY_WINSIZE = 'WinSize';
const KEY_IPV4 = 'Enable IPv4';
const KEY_IPV6 = 'Enable IPv6';
const KEY_LOCALIP = 'LocalIP';
const KEY_MCAST_ADDR = 'MulticastAddress';
const KEY_MCAST_PORT = 'MulticastPort';
const KEY_SYSLOG_FILE = 'SaveSyslogFile';
const KEY_SERVICES = 'Services';
const BUNDLE_NAME = 'io.github.hdsagb.tftpd32harmongyOS';
const ABILITY_NAME = 'EntryAbility';

// D2：同一进程内只自动启动一次（页面重建/通知授权弹窗不应重复拉起服务）
let gAutoStarted: boolean = false;

// PH-19：DATA_TRANSFER 长时任务超过 10 分钟无进度更新会被系统取消，进程随后被冻结
// （表现为端口仍在监听、收包队列堆积、对端全部超时）。取消回调只注册一次，转发给当前页面实例。
let gCtListenerInstalled: boolean = false;
let gCtRetry: () => void = () => {};

@Entry
@Component
struct Index {
  @State port: string = '6900';
  @State baseDir: string = '';
  @State security: number = 1;      // SECURITY_STD，与原版一致
  @State timeout: string = '3';
  @State retransmit: string = '6';
  @State negociate: boolean = true;
  @State winSize: string = '1';
  @State ipv4On: boolean = true;    // 原版 Enable IPv4 键，默认开启
  @State ipv6On: boolean = true;    // 原版 Enable IPv6 键，默认开启
  @State localIp: string = '';
  @State localIpIndex: number = 0;
  private localIpOpts: SelectOption[] = [{ value: '全部接口' }];
  @State mcastAddr: string = '';
  @State mcastPort: string = '';
  @State syslogFile: string = '';
  @State running: boolean = false;
  @State syslogOn: boolean = false;
  @State sntpOn: boolean = false;
  @State dnsOn: boolean = false;
  @State dhcpOn: boolean = false;
  @State logs: string[] = [];
  @State transfers: TrfStat[] = [];
  private ctRunning: boolean = false;   // 长时任务是否申请成功
  private ctId: number = -1;            // 当前长时任务 ID
  private ctKeepTimer: number = -1;     // 定期刷新长时任务（延后系统取消）的定时器
  private ctKeepWarned: boolean = false;
  private static readonly NOTIFY_ID = 1001;
  private logScroller: Scroller = new Scroller();
  // 日志自动吸底：仅在用户停留在列表底部时才跟随新日志；
  // 用户上翻查看历史时保持当前位置，不再被强制拉回底部
  private logFollow: boolean = true;
  private pruneTimer: number = -1;
  private securityOpts: SelectOption[] = [
    { value: '无' }, { value: '标准' }, { value: '高' }, { value: '只读' }
  ];

  aboutToAppear(): void {
    TftpdService.init(getContext(this));
    this.loadSettings();
    this.baseDir = TftpdService.baseDir;   // D12：根目录跟随 ini BaseDirectory
    TftpdService.subscribe(() => {
      this.logs = [...TftpdService.logs];
      this.transfers = [...TftpdService.transfers];
      if (this.logFollow) this.logScroller.scrollEdge(Edge.Bottom);
    });
    // 已结束行留存 10s 后清除（对齐原版 Keep transfer Gui）
    this.pruneTimer = setInterval(() => {
      if (TftpdService.pruneEnded()) this.transfers = [...TftpdService.transfers];
    }, 1000);
    // D2：与原版一致，启动应用即启动已启用的服务；失败时保持“已停止”，由用户手动开启
    this.running = TftpdService.isStarted();
    if (!gAutoStarted) {
      gAutoStarted = true;
      this.autoStart();
    }
    // PH-19：长时任务被系统取消时立即重申请，避免进程随后被冻结
    gCtRetry = () => this.reEnsureContinuousTask();
    if (!gCtListenerInstalled) {
      gCtListenerInstalled = true;
      try {
        backgroundTaskManager.on('continuousTaskCancel',
          (info: backgroundTaskManager.ContinuousTaskCancelInfo) => {
            TftpdService.logs.push(`长时任务被系统取消（原因码 ${info.reason}），正在重新申请`);
            gCtRetry();
          });
      } catch (_e) { /* 设备无此接口时的降级路径 */ }
    }
    // PH-19：定时刷新任务，尽量不让系统走到“10 分钟无进度”的取消路径
    this.ctKeepTimer = setInterval(() => this.refreshContinuousTask(), 4 * 60 * 1000);
  }

  onPageShow(): void {
    // 回到前台时若长时任务已失效则重新申请，否则再次退后台仍会被冻结
    this.running = TftpdService.isStarted();
    if (this.running && !this.ctRunning) this.reEnsureContinuousTask();
  }

  aboutToDisappear(): void {
    if (this.pruneTimer !== -1) clearInterval(this.pruneTimer);
    if (this.ctKeepTimer !== -1) clearInterval(this.ctKeepTimer);
  }

  build() {
    Column() {
      Text('Tftpd32 for HarmonyOS').fontSize(16).fontWeight(FontWeight.Bold).margin(8)
      Tabs() {
        TabContent() { this.SettingsPage() }.tabBar('设置')
        TabContent() { this.TftpPage() }.tabBar('TFTP')
        TabContent() { this.LogPage() }.tabBar('日志')
        TabContent() { SyslogPage() }.tabBar('Syslog')
        TabContent() { SntpPage() }.tabBar('SNTP')
        TabContent() { DnsPage() }.tabBar('DNS')
        TabContent() { DhcpPage() }.tabBar('DHCP')
        TabContent() { ClientPage() }.tabBar('客户端')
        TabContent() { DirectoryPage() }.tabBar('目录')
      }.layoutWeight(1).barMode(BarMode.Scrollable)
    }.height('100%')
  }

  @Builder
  SettingsPage() {
    Scroll() {
      Row() {
        Column({ space: 8 }) {
          Row() {
            Text('服务').width(80)
            Toggle({ type: ToggleType.Switch, isOn: this.running })
              .onChange((on: boolean) => { this.toggleService(on) })
            Text(this.running ? '运行中' : '已停止').margin({ left: 8 })
          }.width('100%')

          Row() {
            Text('Syslog 服务器').width(160)
            Toggle({ type: ToggleType.Switch, isOn: this.syslogOn })
              .onChange((on: boolean) => { this.syslogOn = on; this.toggleSub(0x0008, on) })
            Text('端口 ' + (TftpdService.getInt('SyslogPort') || 5140)).fontSize(12).opacity(0.6).margin({ left: 8 })
            Blank()
          }.width('100%')

          Row() {
            Text('SNTP 服务器').width(160)
            Toggle({ type: ToggleType.Switch, isOn: this.sntpOn })
              .onChange((on: boolean) => { this.sntpOn = on; this.toggleSub(0x0010, on) })
            Text('端口 ' + (TftpdService.getInt('SntpPort') || 1230)).fontSize(12).opacity(0.6).margin({ left: 8 })
            Blank()
          }.width('100%')

          Row() {
            Text('DNS 服务器').width(160)
            Toggle({ type: ToggleType.Switch, isOn: this.dnsOn })
              .onChange((on: boolean) => { this.dnsOn = on; this.toggleSub(0x0020, on) })
            Text('端口 ' + (TftpdService.getInt('DnsPort') || 5300)).fontSize(12).opacity(0.6).margin({ left: 8 })
            Blank()
          }.width('100%')

          Row() {
            Text('DHCP 服务器').width(160)
            Toggle({ type: ToggleType.Switch, isOn: this.dhcpOn })
              .onChange((on: boolean) => { this.dhcpOn = on; this.toggleSub(0x0004, on) })
            Text('端口 ' + TftpdService.getInt('DhcpPort') + '（在 DHCP 页签修改）')
              .fontSize(12).opacity(0.6).margin({ left: 8 }).layoutWeight(1)
            Blank()
          }.width('100%')

          Row() {
            Text('端口').width(80)
            TextInput({ text: this.port }).width(120).type(InputType.Number)
              .onChange(v => { this.port = v })
            Text('默认 6900（应用无法绑定 69 等特权端口）').fontSize(12).opacity(0.6).layoutWeight(1)
          }.width('100%')

          Row() {
            Text('绑定网卡（LocalIP）').width(160)
            Select(this.localIpOpts)
              .selected(this.localIpIndex).value(this.localIp || '全部接口')
              .onSelect((i: number) => {
                this.localIpIndex = i;
                const v = `${this.localIpOpts[i].value}`;
                this.localIp = i === 0 ? '' : v;
              })
            Blank()
          }.width('100%')

          Text('留空表示监听全部接口；选择具体地址后仅在该网卡上响应（原版 LocalIP 语义）')
            .fontSize(12).opacity(0.6)

          Text('后台运行：服务运行期间申请长时任务（dataTransfer）以保持端口监听；' +
            '若系统提示耗电，请在“设置 > 电池 > 耗电量排行”中找到本应用，进入应用耗电详情后允许后台活动')
            .fontSize(12).opacity(0.6)

          Row() {
            Text('根目录').width(80)
            Text(this.baseDir).fontSize(12).layoutWeight(1)
              .maxLines(1).textOverflow({ overflow: TextOverflow.Ellipsis })
          }.width('100%')

          Row() {
            Text('安全级别').width(80)
            Select(this.securityOpts)
              .selected(this.security).value(this.securityText())
              .onSelect((i: number) => { this.security = i })
          }.width('100%')

          Row() {
            Text('超时(秒)').width(80)
            TextInput({ text: this.timeout }).width(80).type(InputType.Number)
              .onChange(v => { this.timeout = v })
            Text('重传次数').width(80).margin({ left: 12 })
            TextInput({ text: this.retransmit }).width(80).type(InputType.Number)
              .onChange(v => { this.retransmit = v })
          }.width('100%')

          Row() {
            Text('选项协商').width(80)
            Toggle({ type: ToggleType.Switch, isOn: this.negociate })
              .onChange(on => { this.negociate = on })
            Text('RFC2347/2348/2349').fontSize(12).opacity(0.6).margin({ left: 8 })
          }.width('100%')

          Row() {
            Text('WinSize（RFC7440）').width(180)
            TextInput({ text: this.winSize }).width(120).type(InputType.Number)
              .onChange(v => { this.winSize = v })
          }.width('100%')

          Row() {
            Text('启用 IPv4').width(180)
            Toggle({ type: ToggleType.Switch, isOn: this.ipv4On })
              .onChange(on => { this.ipv4On = on })
            Text('关闭后 TFTP 仅按 IPv6 监听（原版 Enable IPv4 键），重启服务生效')
              .fontSize(12).opacity(0.6).margin({ left: 8 }).layoutWeight(1)
          }.width('100%')

          Row() {
            Text('启用 IPv6').width(180)
            Toggle({ type: ToggleType.Switch, isOn: this.ipv6On })
              .onChange(on => { this.ipv6On = on })
            Text('关闭后 TFTP 不监听 IPv6（原版 Enable IPv6 键），重启服务生效')
              .fontSize(12).opacity(0.6).margin({ left: 8 }).layoutWeight(1)
          }.width('100%')

          Row() {
            Text('组播地址（RFC2090）').width(180)
            TextInput({ text: this.mcastAddr, placeholder: '如 239.255.0.1' }).layoutWeight(1)
              .onChange(v => { this.mcastAddr = v })
          }.width('100%')

          Row() {
            Text('组播端口').width(180)
            TextInput({ text: this.mcastPort, placeholder: '6969' }).width(120).type(InputType.Number)
              .onChange(v => { this.mcastPort = v })
          }.width('100%')

          Text('组播留空即关闭；客户端 RRQ 带 multicast 选项时按此地址下发 DATA（须为 224.0.0.0/4）')
            .fontSize(12).opacity(0.6)

          Row() {
            Text('Syslog 日志文件').width(180)
            TextInput({ text: this.syslogFile }).layoutWeight(1)
              .onChange(v => { this.syslogFile = v })
          }.width('100%')

          Blank()
          Button('保存配置').width('100%')
            .onClick(() => {
              const ok = this.applySettings();     // 先把界面值写入设置并落盘
              if (ok) TftpdService.logs.push('配置已保存');   // 失败原因由 applySettings 记录
              this.logs = [...TftpdService.logs];
            })

          Divider().margin({ top: 16, bottom: 8 })

          Text('关于').fontSize(14).fontWeight(FontWeight.Bold)
          Text('本应用是基于开源项目 Tftpd32/Tftpd64（作者 Philippe Jounin，EUPL-1.2）的鸿蒙移植版，' +
            '为独立开发的第三方版本，与原作者及官方发布无关。')
            .fontSize(12).opacity(0.6).margin({ top: 4 })
          Text('本项目按 GPL-2.0-only 分发，完整源码随仓库公开；应用图标为本项目自制，未使用上游品牌素材。')
            .fontSize(12).opacity(0.6).margin({ top: 4 })
        }.padding(12).alignItems(HorizontalAlign.Start)
          .width('100%').constraintSize({ maxWidth: 720 })   // 横屏时限制内容宽度并居中，避免行被拉得过宽
      }.width('100%').justifyContent(FlexAlign.Center)
    }.scrollBar(BarState.Auto).height('100%')
  }

  private securityText(): string {
    return ['无', '标准', '高', '只读'][this.security] ?? '标准';
  }

  // 原版 gui_tftpd.c AddNewTftpItem：文件名首尾带方向符（<下载 / >上传），
  // 结束后 ManageTerminatedTransfers 把首字符替换为 '.'（ZOMBIE_STATUS）
  private static trfName(t: TrfStat): string {
    const f = t.file !== undefined ? t.file : '';
    const d = t.opcode === 2 ? '>' : '<';
    const cell = `${d}${f}${d}`;
    return t.done === true ? `.${cell.substring(1)}` : cell;
  }

  private static percent(t: TrfStat): number {
    if (t.size <= 0) return 0;
    const p = Math.floor(t.bytes * 100 / t.size);
    return p < 0 ? 0 : (p > 100 ? 100 : p);
  }

  // 原版 GUI 的“进度”列（gui_tftpd.c UpdateTftpItem）：
  // 运行中显示百分比（总长不足 100 字节时显示 N/A），结束显示 100% / STPD / ERR
  private static progLabel(t: TrfStat): string {
    if (t.done === true && t.rc !== undefined) {
      if (t.rc === 1) return '100%';
      if (t.rc === 2) return 'STPD';
      return 'ERR';
    }
    if (t.size >= 100) return `${Math.floor(t.bytes / (t.size / 100))}%`;
    return 'N/A';
  }

  private loadSettings(): void {
    const p = TftpdService.getInt(KEY_PORT);
    if (p > 0) this.port = `${p}`;
    const s = TftpdService.getInt(KEY_SECURITY);
    if (s >= 0) this.security = s;
    const t = TftpdService.getInt(KEY_TIMEOUT);
    if (t > 0) this.timeout = `${t}`;
    const r = TftpdService.getInt(KEY_RETRANSMIT);
    if (r > 0) this.retransmit = `${r}`;
    const n = TftpdService.getInt(KEY_NEGOCIATE);
    if (n >= 0) this.negociate = n !== 0;
    const w = TftpdService.getInt(KEY_WINSIZE);   /* 整型键，须用 getInt 读取 */
    this.winSize = w > 0 ? `${w}` : '1';
    const v4 = TftpdService.getInt(KEY_IPV4);
    if (v4 >= 0) this.ipv4On = v4 !== 0;
    const v6 = TftpdService.getInt(KEY_IPV6);
    if (v6 >= 0) this.ipv6On = v6 !== 0;
    // LocalIP：枚举本机网卡地址，回填已保存的选择
    const ips = TftpdService.localIPv4s();
    const opts: SelectOption[] = [{ value: '全部接口' }];
    ips.forEach((ip: string) => { opts.push({ value: ip }); });
    this.localIpOpts = opts;
    this.localIp = TftpdService.getStr(KEY_LOCALIP);
    const idx = this.localIpOpts.findIndex((o: SelectOption) => `${o.value}` === this.localIp);
    this.localIpIndex = idx > 0 ? idx : 0;
    if (idx <= 0) this.localIp = '';
    this.mcastAddr = TftpdService.getStr(KEY_MCAST_ADDR);
    const mp = TftpdService.getInt(KEY_MCAST_PORT);
    this.mcastPort = mp > 0 ? `${mp}` : '';
    this.syslogFile = TftpdService.getStr(KEY_SYSLOG_FILE);
    const svc = TftpdService.getInt(KEY_SERVICES);
    const mask = svc > 0 ? svc : 0x0001;
    this.syslogOn = (mask & 0x0008) !== 0;
    this.sntpOn = (mask & 0x0010) !== 0;
    this.dnsOn = (mask & 0x0020) !== 0;
    this.dhcpOn = (mask & 0x0004) !== 0;
  }

  private serviceMask(): number {
    let m = 0x0001;
    if (this.syslogOn) m |= 0x0008;
    if (this.sntpOn) m |= 0x0010;
    if (this.dnsOn) m |= 0x0020;
    if (this.dhcpOn) m |= 0x0004;
    return m;
  }

  private toggleSub(mask: number, on: boolean): void {
    const cur = TftpdService.getInt(KEY_SERVICES);
    let m = cur > 0 ? cur : 0x0001;
    m = on ? (m | mask) : (m & ~mask);
    TftpdService.setInt(KEY_SERVICES, m);
    TftpdService.save();
    if (this.running) {
      if (on) TftpdService.startMask(mask);
      else TftpdService.stopMask(mask);
    }
  }

  // D1：端口须为 1~65535 的整数；非法输入不再静默回退到 6900
  private static parsePort(text: string): number {
    const s = text.trim();
    if (s === '') return -1;
    const v = Number(s);
    if (!Number.isInteger(v) || v < 1 || v > 65535) return -1;
    return v;
  }

  private applySettings(): boolean {
    const port = Index.parsePort(this.port);
    if (port < 0) {
      TftpdService.logs.push(`端口“${this.port}”无效：应为 1~65535 的整数，配置未保存`);
      return false;
    }
    TftpdService.setInt(KEY_PORT, port);
    TftpdService.setInt(KEY_SECURITY, this.security);
    TftpdService.setInt(KEY_TIMEOUT, parseInt(this.timeout) || 3);
    TftpdService.setInt(KEY_RETRANSMIT, parseInt(this.retransmit) || 6);
    TftpdService.setInt(KEY_NEGOCIATE, this.negociate ? 1 : 0);
    TftpdService.setInt(KEY_WINSIZE, Number(this.winSize));
    TftpdService.setInt(KEY_IPV4, this.ipv4On ? 1 : 0);
    TftpdService.setInt(KEY_IPV6, this.ipv6On ? 1 : 0);
    TftpdService.setStr(KEY_LOCALIP, this.localIp);
    TftpdService.setStr(KEY_MCAST_ADDR, this.mcastAddr.trim());
    TftpdService.setInt(KEY_MCAST_PORT, parseInt(this.mcastPort) || 0);
    TftpdService.setStr(KEY_SYSLOG_FILE, this.syslogFile);
    TftpdService.setInt(KEY_SERVICES, this.serviceMask());
    TftpdService.setStr(KEY_BASEDIR, this.baseDir);
    return TftpdService.save();
  }

  private async startContinuousTask(): Promise<void> {
    const info: wantAgent.WantAgentInfo = {
      wants: [{ bundleName: BUNDLE_NAME, abilityName: ABILITY_NAME }],
      operationType: wantAgent.OperationType.START_ABILITY,
      requestCode: 0,
      wantAgentFlags: [wantAgent.WantAgentFlags.UPDATE_PRESENT_FLAG]
    };
    const wantAgentObj = await wantAgent.getWantAgent(info);
    const nt = await backgroundTaskManager.startBackgroundRunning(
      getContext(this), ['dataTransfer'], wantAgentObj);
    this.ctId = nt.continuousTaskId ?? -1;
  }

  // PH-19：DATA_TRANSFER 超过 10 分钟无进度更新会被系统取消，定期刷新以延长任务寿命
  private refreshContinuousTask(): void {
    if (!this.ctRunning || !TftpdService.isStarted()) return;
    backgroundTaskManager.updateBackgroundRunning(getContext(this), ['dataTransfer'])
      .then((nt: backgroundTaskManager.ContinuousTaskNotification) => {
        this.ctId = nt.continuousTaskId ?? this.ctId;
      })
      .catch((e: BusinessError) => {
        // 刷新失败不影响服务，改由取消回调兜底；只提示一次，避免刷屏
        if (!this.ctKeepWarned) {
          this.ctKeepWarned = true;
          TftpdService.logs.push(`提示：长时任务刷新失败（${e?.code ?? '?'}），改为被取消即重新申请`);
        }
      });
  }

  // 长时任务被系统取消（或回前台发现已失效）后重新申请
  private reEnsureContinuousTask(): void {
    this.ctRunning = false;
    if (!TftpdService.isStarted()) return;
    setTimeout(() => {
      if (!TftpdService.isStarted()) return;
      this.startContinuousTask().then(() => {
        this.ctRunning = true;
        this.ctKeepWarned = false;
        TftpdService.logs.push('长时任务已重新申请');
      }).catch((e: BusinessError) => {
        this.ctRunning = false;
        TftpdService.logs.push(`提示：长时任务重新申请失败（${Index.bgErrText(e?.code ?? 0)}），` +
          '可在“设置 > 电池”中允许本应用后台活动');
        this.publishRunningNotice(`服务运行中（端口 ${this.port}）`);
      });
      this.logs = [...TftpdService.logs];
    }, 1000);
  }

  // 长时任务不可用时（部分手机/模拟器返回 9800005），退化为普通常驻通知
  private publishRunningNotice(text: string): void {
    const req: notificationManager.NotificationRequest = {
      id: Index.NOTIFY_ID,
      content: {
        notificationContentType: notificationManager.ContentType.NOTIFICATION_CONTENT_BASIC_TEXT,
        normal: { title: 'Tftpd32', text: text, additionalText: '' }
      }
    };
    notificationManager.publish(req).catch((e: BusinessError) => {
      if (e?.code === 1600004) {
        // 通知开关未开启：弹出授权后重发一次，交由用户决定
        notificationManager.requestEnableNotification(getContext(this) as common.UIAbilityContext)
          .then(() => notificationManager.publish(req))
          .catch((e2: BusinessError) => {
            TftpdService.logs.push(`提示：通知发布失败（${e2?.code ?? '?'}）`);
          });
        return;
      }
      TftpdService.logs.push(`提示：通知发布失败（${e?.code ?? '?'}）`);
    });
  }

  private cancelRunningNotice(): void {
    notificationManager.cancel(Index.NOTIFY_ID).catch((e: BusinessError) => {
      TftpdService.logs.push(`提示：通知取消失败（${e?.code ?? '?'}）`);
    });
  }

  // 长时任务/通知被拒时的用户可读原因（D3）
  private static bgErrText(code: number): string {
    if (code === 9800005) return '系统未授予长时任务（该设备或模拟器不支持）';
    if (code === 201) return '应用没有后台运行权限';
    if (code === 1600004) return '通知权限未开启';
    return `错误码 ${code}`;
  }

  private autoStart(): void {
    if (this.running) return;
    this.toggleService(true).catch((e: BusinessError) => {
      this.running = false;
      TftpdService.logs.push(`自动启动失败：${Index.bgErrText(e?.code ?? 0)}`);
      this.logs = [...TftpdService.logs];
    });
  }

  private async toggleService(on: boolean): Promise<void> {
    // 已启动时不重复申请长时任务/重启原生服务（页面可能被重建多次）
    if (on && TftpdService.isStarted()) {
      this.running = true;
      return;
    }
    // 端口非法时不启动（停止操作不受表单校验影响），开关回落到实际运行状态
    if (on && !this.applySettings()) {
      this.running = TftpdService.isStarted();
      this.logs = [...TftpdService.logs];
      return;
    }
    if (on) {
      try {
        await this.startContinuousTask();
        this.ctRunning = true;
      } catch (e) {
        // 手机端不支持 dataTransfer 长时任务时降级：仅前台可用 + 常驻通知提示
        const err = e as BusinessError;
        this.ctRunning = false;
        TftpdService.logs.push(`提示：无法保持后台运行（${Index.bgErrText(err?.code ?? 0)}），` +
          '退到后台后服务可能中断；可在“设置 > 电池”中允许本应用后台活动');
      }
      // 申请后台任务期间可能有第二个页面实例进入，落地前再确认一次
      if (TftpdService.isStarted()) {
        this.running = true;
        return;
      }
      this.running = TftpdService.start();
      if (this.running && !this.ctRunning) this.publishRunningNotice(`服务运行中（端口 ${this.port}）`);
      if (!this.running) {
        // 回滚：TFTP 绑定失败时其它子服务可能已启动成功，一并停掉避免残留监听
        TftpdService.stopAll();
        TftpdService.logs.push('TFTP 服务启动失败，请查看日志中的绑定失败原因（端口被占用或无权限）');
      }
    } else {
      TftpdService.stop();
      this.running = false;
      try { if (this.ctRunning) await backgroundTaskManager.stopBackgroundRunning(getContext(this)); } catch (_e) { /* 未申请过 */ }
      this.ctRunning = false;
      this.cancelRunningNotice();
      TftpdService.logs.push('TFTP 服务已停止');
    }
    this.logs = [...TftpdService.logs];
  }

  @Builder
  TftpPage() {
    Column() {
      if (this.transfers.length === 0) {
        Text('当前无传输').margin(20).opacity(0.5)
      } else {
        List({ space: 4 }) {
          ForEach(this.transfers, (t: TrfStat) => {
            ListItem() {
              Column({ space: 2 }) {
                Text(`#${t.id}  ${Index.trfName(t)}`)
                  .fontSize(13).maxLines(1)
                  .textOverflow({ overflow: TextOverflow.Ellipsis })
                Text(`${t.peer ?? ''}   ${t.start ?? ''}`)
                  .fontSize(11).opacity(0.5).maxLines(1)
                Text(`${t.bytes}/${t.size > 0 ? t.size : 'unknown'} 字节   ret=${t.ret}   ${Index.progLabel(t)}`)
                  .fontSize(11).opacity(0.7)
                Stack({ alignContent: Alignment.Start }) {
                  Row().width('100%').height(4).backgroundColor('#E5E5E5').borderRadius(2)
                  Row().width(`${Index.percent(t)}%`).height(4)
                    .backgroundColor(t.done === true && t.rc !== 1 ? '#C94F4F' : '#0A59F7').borderRadius(2)
                }.width('100%').height(4)
              }.padding(6)
            }
          }, (t: TrfStat) => `${t.id}_${t.file}_${t.peer}_${t.bytes}_${t.ret}_${t.done}_${t.rc}`)
        }.layoutWeight(1)
      }
    }.padding(8)
  }

  @Builder
  LogPage() {
    List({ space: 2, scroller: this.logScroller }) {
      ForEach(this.logs, (l: string, i: number) => {
        ListItem() { Text(l).fontSize(12).fontFamily('monospace') }
      }, (l: string, i: number) => `${i}_${l}`)
    }.padding(8).layoutWeight(1)
      // 记录当前视口是否停在末尾：滚到底部时恢复自动吸底，上翻时暂停
      .onScrollIndex((_first: number, last: number) => {
        this.logFollow = last >= this.logs.length - 1;
      })
  }
}

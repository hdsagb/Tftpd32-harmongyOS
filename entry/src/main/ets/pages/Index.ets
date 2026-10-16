import { TftpdService, TrfStat } from '../services/TftpdService';
import backgroundTaskManager from '@ohos.resourceschedule.backgroundTaskManager';
import { wantAgent } from '@kit.AbilityKit';

// 设置键名与原版 Tftpd32 注册表键完全一致（兼容性强原则）
const KEY_PORT = 'TftpPort';
const KEY_BASEDIR = 'BaseDirectory';
const KEY_SECURITY = 'SecurityLevel';
const KEY_TIMEOUT = 'Timeout';
const KEY_RETRANSMIT = 'MaxRetransmit';
const KEY_NEGOCIATE = 'Negociate';
const BUNDLE_NAME = 'com.example.myapplication';
const ABILITY_NAME = 'EntryAbility';

@Entry
@Component
struct Index {
  @State port: string = '6900';
  @State baseDir: string = '';
  @State security: number = 1;      // SECURITY_STD，与原版一致
  @State timeout: string = '3';
  @State retransmit: string = '6';
  @State negociate: boolean = true;
  @State running: boolean = false;
  @State logs: string[] = [];
  @State transfers: TrfStat[] = [];
  private logScroller: Scroller = new Scroller();
  private securityOpts: SelectOption[] = [
    { value: '无' }, { value: '标准' }, { value: '高' }, { value: '只读' }
  ];

  aboutToAppear(): void {
    TftpdService.init(getContext(this));
    this.baseDir = `${getContext(this).filesDir}/tftpboot`;
    TftpdService.subscribe(() => {
      this.logs = [...TftpdService.logs];
      this.transfers = [...TftpdService.transfers];
      this.logScroller.scrollEdge(Edge.Bottom);
    });
  }

  build() {
    Column() {
      Text('Tftpd32 for HarmonyOS').fontSize(16).fontWeight(FontWeight.Bold).margin(8)
      Tabs() {
        TabContent() { this.SettingsPage() }.tabBar('设置')
        TabContent() { this.TftpPage() }.tabBar('TFTP')
        TabContent() { this.LogPage() }.tabBar('日志')
      }.layoutWeight(1)
    }.height('100%')
  }

  @Builder
  SettingsPage() {
    Column({ space: 8 }) {
      Row() {
        Text('服务').width(80)
        Toggle({ type: ToggleType.Switch, isOn: this.running })
          .onChange((on: boolean) => { this.toggleService(on) })
        Text(this.running ? '运行中' : '已停止').margin({ left: 8 })
      }.width('100%')

      Row() {
        Text('端口').width(80)
        TextInput({ text: this.port }).width(120).type(InputType.Number)
          .onChange(v => { this.port = v })
        Text('默认 6900（应用无法绑定 69 等特权端口）').fontSize(12).opacity(0.6).layoutWeight(1)
      }.width('100%')

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

      Blank()
      Button('保存配置').width('100%')
        .onClick(() => {
          const ok = TftpdService.save();
          TftpdService.logs.push(ok ? '配置已保存' : '保存失败');
          this.logs = [...TftpdService.logs];
        })
    }.padding(12).alignItems(HorizontalAlign.Start)
  }

  private securityText(): string {
    return ['无', '标准', '高', '只读'][this.security] ?? '标准';
  }

  private applySettings(): void {
    TftpdService.setInt(KEY_PORT, parseInt(this.port) || 6900);
    TftpdService.setInt(KEY_SECURITY, this.security);
    TftpdService.setInt(KEY_TIMEOUT, parseInt(this.timeout) || 3);
    TftpdService.setInt(KEY_RETRANSMIT, parseInt(this.retransmit) || 6);
    TftpdService.setInt(KEY_NEGOCIATE, this.negociate ? 1 : 0);
    TftpdService.setStr(KEY_BASEDIR, this.baseDir);
    TftpdService.save();
  }

  private async startContinuousTask(): Promise<void> {
    const info: wantAgent.WantAgentInfo = {
      wants: [{ bundleName: BUNDLE_NAME, abilityName: ABILITY_NAME }],
      operationType: wantAgent.OperationType.START_ABILITY,
      requestCode: 0,
      wantAgentFlags: [wantAgent.WantAgentFlags.UPDATE_PRESENT_FLAG]
    };
    const wantAgentObj = await wantAgent.getWantAgent(info);
    await backgroundTaskManager.startBackgroundRunning(
      getContext(this),
      backgroundTaskManager.BackgroundMode.TASK_KEEPING,
      wantAgentObj);
  }

  private async toggleService(on: boolean): Promise<void> {
    this.applySettings();
    if (on) {
      try {
        await this.startContinuousTask();
      } catch (e) {
        // 手机端不支持 taskKeeping 时降级：仅前台可用，日志提示
        TftpdService.logs.push('提示：此设备不支持后台常驻，退后台后服务可能中断');
      }
      this.running = TftpdService.start();
      TftpdService.logs.push(this.running ? `TFTP 服务已启动（端口 ${this.port}）` : 'TFTP 服务启动失败（端口被占用？）');
    } else {
      TftpdService.stop();
      this.running = false;
      try { await backgroundTaskManager.stopBackgroundRunning(getContext(this)); } catch (_e) { /* 未申请过 */ }
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
                Text(t.file !== undefined && t.file !== ''
                  ? `#${t.id}  ${t.file}`
                  : `传输 #${t.id}`)
                  .fontSize(13).maxLines(1)
                  .textOverflow({ overflow: TextOverflow.Ellipsis })
                Text(`${t.bytes}/${t.size} 字节   ${t.dur ?? 0}s   ret=${t.ret}`)
                  .fontSize(11).opacity(0.7)
                Progress({
                  value: t.size > 0 ? Number(BigInt(t.bytes) * 100n / BigInt(t.size)) : 0,
                  total: 100
                }).width('100%')
              }.padding(6)
            }
          }, (t: TrfStat) => `${t.id}_${t.bytes}_${t.ret}`)
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
  }
}

import tftpd from 'libtftpd32.so';
import { preferences } from '@kit.ArkData';
import { fileIo as fs } from '@kit.CoreFileKit';

// 与原版注册表键同名（settings.h KEY_GUI_REMANENCE）
const KEY_GUI_REMANENCE = 'Keep transfer Gui';

export interface TrfStat {
  id: number;
  bytes: number;
  size: number;
  ret: number;
  dur?: number;
  file?: string;
  opcode?: number;   // 1=RRQ（下载），2=WRQ（上传）
  peer?: string;     // 对端 "ip:port"（来自 trf 帧）
  start?: string;    // 启动时刻 "HH:MM:SS"（来自 trf 帧）
  rc?: number;       // 结束时状态：0=进行中/成功，见 TFTP_TRF_*
  done?: boolean;    // 传输已结束（行保留在列表中，与原版一致）
  doneAt?: number;   // 结束时刻（毫秒），用于 "Keep transfer Gui" 留存计时
}

// ArkTS 限制：对象字面量不能作类型声明 → 用单一接口表达事件
export interface TftpdEvent {
  type: string;      // 'log' | 'trf' | 'stat' | 'service' | 'started'
  text?: string;
  trfs?: TrfStat[];
  raw?: string;
}

export class TftpdService {
  private static cfgPath: string = '';
  private static listeners: ((e: TftpdEvent) => void)[] = [];
  static readonly logs: string[] = [];
  static readonly transfers: TrfStat[] = [];

  static async init(ctx: Context): Promise<void> {
    TftpdService.cfgPath = `${ctx.filesDir}/tftpd32.ini`;
    // TFTP 根目录：沙箱内固定目录（对齐原版开箱即用；用户选目录授权放里程碑3）
    try {
      fs.accessSync(`${ctx.filesDir}/tftpboot`);
    } catch (_e) {
      try { fs.mkdirSync(`${ctx.filesDir}/tftpboot`); } catch (_e2) { /* 已存在 */ }
    }
    tftpd.setConfigPath(TftpdService.cfgPath);
    tftpd.readSettings();     // 启动即恢复已保存配置，供界面回填
    tftpd.setEventCallback((type: string, json: string) => TftpdService.onEvent(type, json));
  }

  private static onEvent(type: string, json: string): void {
    if (type === 'log') {
      TftpdService.logs.push(json);
      if (TftpdService.logs.length > 500) TftpdService.logs.shift();
    } else if (type === 'trf') {
      try {
        const t = JSON.parse(json) as TrfStat;
        const idx = TftpdService.transfers.findIndex(x => x.id === t.id);
        if (idx >= 0) {
          const cur = TftpdService.transfers[idx];
          if (t.bytes !== undefined) cur.bytes = t.bytes;
          if (t.size !== undefined) cur.size = t.size;
          if (t.ret !== undefined) cur.ret = t.ret;
          if (t.dur !== undefined) cur.dur = t.dur;
          if (t.file !== undefined && t.file !== '') cur.file = t.file;
        } else {
          TftpdService.transfers.push(t);
        }
      } catch (_e) { /* 忽略坏帧 */ }
    } else if (type === 'end') {
      try {
        const t = JSON.parse(json) as TrfStat;
        t.done = true;
        t.doneAt = Date.now();
        const idx = TftpdService.transfers.findIndex(x => x.id === t.id);
        if (idx >= 0) {
          const cur = TftpdService.transfers[idx];
          // end 帧只带统计，文件名/对端/启动时刻/方向沿用 trf 帧
          t.file = cur.file;
          t.peer = cur.peer;
          t.start = cur.start;
          t.opcode = cur.opcode;
          TftpdService.transfers[idx] = t;
        } else {
          TftpdService.transfers.push(t);
        }
      } catch (_e) { /* 忽略坏帧 */ }
    } else if (type === 'stat') {
      try {
        const obj = JSON.parse(json) as Record<string, Object>;
        const rawList = obj['t'];
        if (Array.isArray(rawList)) {
          const list = rawList as TrfStat[];
          // stat 帧只带统计（文件名/对端/启动时刻由 trf 帧单独送达），整表刷新时按 id 保留
          const prev = new Map<number, TrfStat>();
          TftpdService.transfers.forEach(x => prev.set(x.id, x));
          // 已结束的传输行按原版 GUI 语义保留，不随 stat 整表刷新被移除
          const ended = TftpdService.transfers.filter(x => x.done === true);
          TftpdService.transfers.splice(0, TftpdService.transfers.length);
          for (const t of list) {
            const old = prev.get(t.id);
            if (old !== undefined) {
              if (t.file === undefined || t.file === '') t.file = old.file;
              if (t.peer === undefined) t.peer = old.peer;
              if (t.start === undefined) t.start = old.start;
              if (t.opcode === undefined) t.opcode = old.opcode;
            }
            TftpdService.transfers.push(t);
          }
          for (const e of ended) TftpdService.transfers.push(e);
          // 与原版一致：列表最多保留 100 条
          while (TftpdService.transfers.length > 100) TftpdService.transfers.shift();
        }
      } catch (_e) { /* 忽略坏帧 */ }
    }
    const ev = TftpdService.parseEvent(type, json);
    TftpdService.listeners.forEach(fn => fn(ev));
  }

  private static parseEvent(type: string, payload: string): TftpdEvent {
    if (type === 'log') return { type: 'log', text: payload };
    if (type === 'trf' || type === 'stat' || type === 'end') return { type: type, trfs: TftpdService.transfers };
    return { type: 'service', raw: payload };
  }

  static subscribe(fn: (e: TftpdEvent) => void): void {
    TftpdService.listeners.push(fn);
  }

  // 原版 gui_tftpd.c ManageTerminatedTransfers：结束行保留 nGuiRemanence 秒后删除，
  // 键名沿用注册表键 "Keep transfer Gui"，原版默认 10 秒
  static pruneEnded(): boolean {
    let rem = TftpdService.getInt(KEY_GUI_REMANENCE);
    if (rem <= 0) rem = 10;
    const now = Date.now();
    const before = TftpdService.transfers.length;
    for (let i = TftpdService.transfers.length - 1; i >= 0; i--) {
      const t = TftpdService.transfers[i];
      if (t.done === true && t.doneAt !== undefined && now - t.doneAt >= rem * 1000) {
        TftpdService.transfers.splice(i, 1);
      }
    }
    return TftpdService.transfers.length !== before;
  }

  static setInt(key: string, val: number): boolean {
    return tftpd.setSettingInt(key, val);
  }

  static setStr(key: string, val: string): boolean {
    return tftpd.setSettingStr(key, val);
  }

  static save(): boolean {
    return tftpd.saveSettings();
  }

  static getInt(key: string): number {
    const v = tftpd.getSettingInt(key);
    return v === undefined ? -1 : v;
  }

  static start(): boolean {
    return tftpd.startService();
  }

  static stop(): void {
    tftpd.stopService();
  }
}

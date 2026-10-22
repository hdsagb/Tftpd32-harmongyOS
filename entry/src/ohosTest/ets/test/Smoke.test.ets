import { describe, beforeAll, afterAll, it, expect } from '@ohos/hypium';
import { abilityDelegatorRegistry } from '@kit.TestKit';
import { common } from '@kit.AbilityKit';
import { fileIo as fs } from '@kit.CoreFileKit';
import { buffer } from '@kit.ArkTS';
import tftpd from 'libtftpd32.so';

// B1/B2 冒烟：直接驱动 native 层（配置读写、服务启停、回环 TFTP 传输）
// 说明：使用独立的临时 ini 与根目录，不触碰应用自身的 tftpd32.ini

interface ClientState {
  state: string;
  bytes: number;
  size: number;
  blk: number;
  rc: number;
  msg: string;
  md5: string;
}

const APP_CTX: common.Context = abilityDelegatorRegistry.getAbilityDelegator().getAppContext();
const CTX_DIR: string = APP_CTX.cacheDir;
const SMOKE_DIR = `${CTX_DIR}/tftpd_smoke`;
const INI = `${SMOKE_DIR}/tftpd32.ini`;
const BOOT = `${SMOKE_DIR}/boot`;
const PORT = 16900;             // 避开应用默认 6900，避免与已启动实例抢占

let logs: string[] = [];
let lastClient: ClientState = { state: 'idle', bytes: 0, size: 0, blk: 0, rc: 0, msg: '', md5: '' };

function ensureDir(path: string): void {
  try {
    fs.mkdirSync(path, true);
  } catch (_e) { /* 已存在 */ }
}

function writeText(path: string, text: string): void {
  const f = fs.openSync(path, fs.OpenMode.CREATE | fs.OpenMode.READ_WRITE | fs.OpenMode.TRUNC);
  fs.writeSync(f.fd, text);
  fs.closeSync(f.fd);
}

function readText(path: string): string {
  const f = fs.openSync(path, fs.OpenMode.READ_ONLY);
  const st = fs.statSync(path);
  const buf = new ArrayBuffer(st.size);
  fs.readSync(f.fd, buf);
  fs.closeSync(f.fd);
  return buffer.from(buf).toString('utf-8');
}

function sleep(ms: number): Promise<void> {
  return new Promise<void>((resolve) => setTimeout(() => resolve(), ms));
}

async function waitClient(timeoutMs: number): Promise<ClientState> {
  const t0 = Date.now();
  while (Date.now() - t0 < timeoutMs) {
    if (lastClient.state === 'done' || lastClient.state === 'error') {
      return lastClient;
    }
    await sleep(100);
  }
  return lastClient;
}

function resetClient(): void {
  lastClient = { state: 'idle', bytes: 0, size: 0, blk: 0, rc: 0, msg: '', md5: '' };
}

export default function smokeTest() {
  describe('Tftpd32Smoke', () => {
    beforeAll(() => {
      ensureDir(SMOKE_DIR);
      ensureDir(BOOT);
      tftpd.setEventCallback((type: string, json: string) => {
        if (type === 'log') {
          logs.push(json);
        } else if (type === 'client') {
          try {
            lastClient = JSON.parse(json) as ClientState;
          } catch (_e) { /* 忽略坏帧 */ }
        }
      });
      tftpd.setConfigPath(INI);
      tftpd.readSettings();
    });

    afterAll(() => {
      tftpd.stopServiceMask(0x0001);
    });

    // B1-01：原版键名落盘与回读（含 A1 的 Enable IPv4）
    it('B1_config_keys', 0, () => {
      expect(tftpd.setSettingInt('Enable IPv4', 0)).assertTrue();
      expect(tftpd.setSettingInt('TftpPort', PORT)).assertTrue();
      expect(tftpd.saveSettings()).assertTrue();

      expect(tftpd.getSettingInt('Enable IPv4')).assertEqual(0);
      expect(tftpd.getSettingInt('TftpPort')).assertEqual(PORT);

      const ini = readText(INI);
      expect(ini.indexOf('Enable IPv4=0') >= 0).assertTrue();
      expect(ini.indexOf(`TftpPort=${PORT}`) >= 0).assertTrue();

      // 恢复默认，避免影响后续用例
      tftpd.setSettingInt('Enable IPv4', 1);
      tftpd.saveSettings();
    });

    // B1-02：服务启停与启动日志
    it('B1_service_start', 0, async () => {
      expect(tftpd.setSettingStr('BaseDirectory', BOOT)).assertTrue();
      expect(tftpd.saveSettings()).assertTrue();
      tftpd.readSettings();

      logs = [];
      expect(tftpd.startServiceMask(0x0001)).assertTrue();   // 仅 TFTP
      await sleep(1500);

      const started = logs.some((l: string) => l.indexOf('服务已启动') >= 0 && l.indexOf(`${PORT}`) >= 0);
      expect(started).assertTrue();
    });

    // B2-01：回环下载（RRQ）：服务端读到客户端落盘内容
    it('B2_loopback_rrq', 0, async () => {
      const src = `${BOOT}/hello.txt`;
      const dst = `${SMOKE_DIR}/down.txt`;
      writeText(src, 'hello loopback');

      resetClient();
      expect(tftpd.startTftpClient(1, '127.0.0.1', PORT, 'hello.txt', dst, 0)).assertTrue();
      const st = await waitClient(15000);
      expect(st.state).assertEqual('done');
      expect(readText(dst)).assertEqual('hello loopback');
    });

    // B2-02：回环上传（WRQ）：客户端文件落到服务端根目录
    it('B2_loopback_wrq', 0, async () => {
      const src = `${SMOKE_DIR}/up.txt`;
      const dst = `${BOOT}/up.txt`;
      writeText(src, 'upload payload');

      await sleep(300);           // 等上一笔结束清 busy 标志
      resetClient();
      expect(tftpd.startTftpClient(2, '127.0.0.1', PORT, 'up.txt', src, 0)).assertTrue();
      const st = await waitClient(15000);
      expect(st.state).assertEqual('done');
      expect(readText(dst)).assertEqual('upload payload');
    });

    // B3：Enable IPv4=0 / Enable IPv6=1 时仅按 IPv6 监听，经 ::1 回环下载
    // 覆盖 A1 的地址族选择分支（AF_INET6）与 tftpd_thread 的 IPv6 对端处理
    it('B3_ipv6_only_rrq', 0, async () => {
      tftpd.stopServiceMask(0x0001);
      await sleep(500);

      const src = `${BOOT}/hello6.txt`;
      const dst = `${SMOKE_DIR}/down6.txt`;
      writeText(src, 'hello ipv6');

      expect(tftpd.setSettingInt('Enable IPv4', 0)).assertTrue();
      expect(tftpd.setSettingInt('Enable IPv6', 1)).assertTrue();
      expect(tftpd.saveSettings()).assertTrue();
      tftpd.readSettings();
      expect(tftpd.getSettingInt('Enable IPv4')).assertEqual(0);

      logs = [];
      expect(tftpd.startServiceMask(0x0001)).assertTrue();
      await sleep(1500);

      resetClient();
      expect(tftpd.startTftpClient(1, '::1', PORT, 'hello6.txt', dst, 0)).assertTrue();
      const st = await waitClient(15000);
      expect(st.state).assertEqual('done');
      expect(readText(dst)).assertEqual('hello ipv6');

      // 恢复双栈默认，避免污染后续用例
      tftpd.stopServiceMask(0x0001);
      await sleep(300);
      tftpd.setSettingInt('Enable IPv4', 1);
      tftpd.saveSettings();
    });
  });
}

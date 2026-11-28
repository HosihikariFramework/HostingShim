// 冒烟用的最小客户端：连入、说一句话、断开。投递冒烟靠它引发 join / chat / leave。
//
// bedrock-protocol 只装在一处（HOSIHIKARI_BOT_MODULES 指向的 node_modules），不与仓
// 绑定：它是验证脚手架，不是交付物。ESM 不读 NODE_PATH，所以按绝对路径引入。
import { pathToFileURL } from 'node:url';
import { join } from 'node:path';

const modules = process.env.HOSIHIKARI_BOT_MODULES;
if (!modules) throw new Error('set HOSIHIKARI_BOT_MODULES to the directory holding node_modules');
const bedrock = (await import(pathToFileURL(join(modules, 'bedrock-protocol', 'index.js')).href)).default;

const port = Number(process.env.BDS_PORT || 19182);
const username = process.env.BDS_BOT_NAME || 'SmokeBot';
const version = process.env.BDS_VER || undefined;

const client = bedrock.createClient({
  host: '127.0.0.1',
  port,
  username,
  offline: true,
  ...(version ? { version } : {}),
});

const leave = (code) => {
  try {
    client.disconnect();
  } catch {}
  setTimeout(() => process.exit(code), 500);
};

client.on('spawn', () => {
  console.log('SPAWNED');
  // 一句聊天：引发 chat 事件，路径与 join 不同（各自一个原生钩子）。
  //
  // 仍未打通，脚本里标 SKIP：包结构已照协议给全（needs_translation/category/type
  // 的顺序与 body 分支都对），服务端就是不记录。`message_only` 与 `authored` 两个
  // category、`/say` 与 `/me` 两条 command_request 都试过，都无声。要接着查就从
  // 这里开始，别再从空中摸索一遍。
  setTimeout(() => {
    client.queue('text', {
      needs_translation: false,
      category: 'authored',
      type: 'chat',
      source_name: username,
      message: 'hello-from-probe',
      xuid: '',
      platform_chat_id: '',
      has_filtered_message: false,
      filtered_message: '',
    });
    console.log('CHATTED');
  }, 2000);
  setTimeout(() => {
    console.log('LEAVING');
    leave(0);
  }, 6000);
});

client.on('error', (error) => console.log('ERROR ' + error.message));
setTimeout(() => {
  console.log('TIMEOUT');
  process.exit(3);
}, 120000);

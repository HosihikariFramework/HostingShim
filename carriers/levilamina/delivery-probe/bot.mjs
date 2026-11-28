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
  // 一句聊天：引发 chat 事件，路径与 join 不同（各自一个原生钩子）。字段按
  // bedrock-protocol 的文档例子给全，缺一个服务端会静默丢掉这个包。
  setTimeout(() => {
    client.queue('text', {
      type: 'chat',
      needs_translation: false,
      source_name: username,
      xuid: '',
      platform_chat_id: '',
      filtered_message: '',
      message: 'hello-from-probe',
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

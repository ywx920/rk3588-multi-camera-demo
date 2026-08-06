# GitHub备份操作

本仓库保存ZX2603-2 RK3588摄像头、Qt、Web、RKNN源码、配置、模型和调试文档。
Python虚拟环境、下载缓存、构建目录、RKISP Tuner资料和大型压缩包由`.gitignore`
排除，应另行保存到NAS或移动硬盘。

## 当前仓库状态

```text
Remote: git@github.com:ywx920/rk3588-demo-camera.git
Branch: main
SSH: 强制IPv4，使用/home/wx/.ssh/id_ed25519
```

当前网络的IPv6 SSH路径会在握手后卡住，因此仓库已配置：

```bash
git config core.sshCommand \
  'ssh -4 -o IdentitiesOnly=yes -i /home/wx/.ssh/id_ed25519'
```

不要再切换到HTTPS Token；本机后续固定使用SSH。

## 首次关联GitHub

先在GitHub创建一个空的Private仓库，不初始化README或License，然后执行：

```bash
cd /home/wx/2-demoRK
git config user.name "你的GitHub用户名"
git config user.email "你的GitHub邮箱"
git remote add origin git@github.com:ywx920/rk3588-demo-camera.git
git push -u origin main
```

本机不再推荐HTTPS，因为GitHub不接受账号密码，而当前环境使用SSH更稳定。

## 日常备份

```bash
cd /home/wx/2-demoRK
git status
git add -A
git diff --cached --stat
git commit -m "说明本次修改"
git push
```

若`ssh -T git@github.com`长时间无输出，先验证IPv4：

```bash
ssh -4 -T -o IdentitiesOnly=yes \
  -i ~/.ssh/id_ed25519 git@github.com
```

成功认证会显示`Hi ywx920!`，随后以退出码1结束，这是GitHub不提供shell造成的
正常行为，不是认证失败。

## 检查未备份内容

```bash
git status --ignored --short
git check-ignore -v 路径
```

被忽略的大型资料不会上传GitHub，需要单独备份：

```text
ov13850.rar
ov13850_materials/
j7-yolov5-rknn-pc/.venv/
j7-yolov5-rknn-pc/.python/
j7-yolov5-rknn-pc/downloads/
j7-yolov5-rknn-pc/third_party/
```

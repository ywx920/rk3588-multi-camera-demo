# GitHub备份与SSH调试记录

## 1. 初始检查

工作目录`/home/wx/2-demoRK`最初不是Git仓库，总体积约5.1 GB。主要大目录为：

- `j7-yolov5-rknn-pc`约2.8 GB；
- `ov13850_materials`约1.8 GB；
- `ov13850.rar`约632 MB。

大型内容主要来自Python虚拟环境、RKNN Toolkit下载包、第三方源码工作副本和
RKISP Tuner程序，不应进入普通Git历史。

## 2. Git初始化

根目录新增`.gitignore`，排除虚拟环境、下载缓存、build目录、日志、采集码流、
大型工具资料和压缩包。保留：

- Minimal、Qt、Web全部源码；
- IQ JSON与板端测试报告；
- YOLOv5 ONNX及RK3588 RKNN模型；
- Runner源码、部署脚本和完整文档。

初始化结果：170个受控文件，Git对象约38.76 MiB。提交历史：

```text
d6e24bb Initial backup: RK3588 multi-camera Qt Web RKNN project
2ec86bb docs: add project overview and usage guide
```

## 3. HTTPS问题

远程配置为HTTPS后，第一次推送提示`fetch first`，说明GitHub仓库创建时已有初始
提交。随后`git pull`使用账号密码/无效Token认证，GitHub返回：

```text
Invalid username or token. Password authentication is not supported.
```

因此决定固定使用SSH，不继续混用HTTPS与Token。

## 4. SSH卡住原因

本机生成Ed25519密钥并将公钥添加到GitHub。首次连接显示的主机指纹为：

```text
SHA256:+DiY3wvvV6TuJJhbpZisF/zLDA0zPMSvHdkr4UvCOqU
```

该值与GitHub官方Ed25519指纹一致。普通`ssh -T git@github.com`接受指纹后长时间
无响应；诊断发现当前网络存在合成IPv6/代理解析。强制IPv4后立即成功：

```bash
ssh -4 -T -o BatchMode=yes -o ConnectTimeout=10 \
  -o IdentitiesOnly=yes -i ~/.ssh/id_ed25519 git@github.com
```

返回：

```text
Hi ywx920! You've successfully authenticated, but GitHub does not provide shell access.
```

仓库最终配置：

```bash
git remote set-url origin git@github.com:ywx920/rk3588-demo-camera.git
git config core.sshCommand \
  'ssh -4 -o IdentitiesOnly=yes -i /home/wx/.ssh/id_ed25519'
```

## 5. 合并与推送

抓取远程后确认远端只有：

```text
590b085 Initial commit
```

合并无共同历史时，远程和本地README产生add/add冲突。保留本地完整项目README，
生成合并提交：

```text
94f7606 merge: integrate GitHub initial repository
```

最终推送成功：

```text
590b085..94f7606  main -> main
branch 'main' set up to track 'origin/main'
```

## 6. 后续备份

```bash
cd /home/wx/2-demoRK
git status
git add -A
git diff --cached --stat
git commit -m "说明本次修改"
git push
```

若未来更换电脑，需要重新生成并在GitHub添加该电脑自己的SSH公钥，不应复制或
上传现有私钥。

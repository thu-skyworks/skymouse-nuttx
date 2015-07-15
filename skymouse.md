## 下载源码

```
mkdir nuttx-all; cd nuttx-all
git clone https://github.com/taogashi/skymouse-nuttx.git nuttx
git clone https://bitbucket.org/nuttx/apps.git apps 
cd nuttx
git submoudle init
git submodule update
```

## 提交代码
### 对configs中内容的修改

```
cd configs
git add <new file>
git commit -m <your message>
git push forked add_skymouse
cd ..
git add configs
git commit -m <your message>
git push forked add_skymouse
```

*TODO*: 说明提交pull request的方法
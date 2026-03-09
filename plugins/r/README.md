# R plugin
[![Join the chat at https://gitter.im/texmacs/Lobby](https://badges.gitter.im/texmacs/Lobby.svg)](https://gitter.im/texmacs/Lobby?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

The R plugin is builtin for GNU TeXmacs.

To develop or customize it, just install it to `$ATHENA_HOME_PATH`:

```
git clone git@github.com:texmacs/plugins.git
cd plugins/r
make
# rm -rf $ATHENA_HOME_PATH/plugins/r
cp -r ../r $ATHENA_HOME_PATH/plugins
```

For GNU/Linux and macOS, `$ATHENA_HOME_PATH` is `$HOME/.TeXmacs/`.

For Windows, `$ATHENA_HOME_PATH` is `%APPDATA%\TeXmacs\`.

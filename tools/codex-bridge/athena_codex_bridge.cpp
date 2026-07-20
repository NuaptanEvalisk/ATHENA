#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <cstdio>
#include <functional>

#ifndef ATHENA_CODEX_CLIENT_VERSION
#define ATHENA_CODEX_CLIENT_VERSION "unknown"
#endif

namespace {

constexpr char dataBegin= 2;
constexpr char dataEnd= 5;
constexpr char dataEscape= 27;

QByteArray
escapeProtocol (QByteArray text) {
  QByteArray escaped;
  escaped.reserve (text.size ());
  for (char c: text) {
    if (c == dataBegin || c == dataEnd || c == dataEscape)
      escaped.append (dataEscape);
    escaped.append (c);
  }
  return escaped;
}

void
emitVerbatim (const QString& text) {
  QByteArray payload= escapeProtocol (text.toUtf8 ());
  std::fputc (dataBegin, stdout);
  std::fputs ("verbatim:", stdout);
  std::fwrite (payload.constData (), 1, payload.size (), stdout);
  std::fputc (dataEnd, stdout);
  std::fflush (stdout);
}

QString
resolveCodex (const QString& requested) {
  if (!requested.isEmpty () && QFileInfo (requested).isExecutable ())
    return QFileInfo (requested).absoluteFilePath ();
#ifdef Q_OS_WIN
  const QString bundledName= "codex.exe";
#else
  const QString bundledName= "codex";
#endif
  const QString bundled= QDir (QCoreApplication::applicationDirPath ())
                           .filePath (bundledName);
  if (QFileInfo (bundled).isExecutable ()) return bundled;
  return QStandardPaths::findExecutable (bundledName);
}

class AppServer {
public:
  using NotificationHandler=
    std::function<void (const QString&, const QJsonObject&)>;

  bool start (const QString& codex, const QString& codexHome,
              QString& error) {
    QDir ().mkpath (codexHome);
    QProcessEnvironment env= QProcessEnvironment::systemEnvironment ();
    env.insert ("CODEX_HOME", codexHome);
    process.setProcessEnvironment (env);
    process.setProcessChannelMode (QProcess::SeparateChannels);
    process.start (codex, {"app-server", "--listen", "stdio://"});
    if (!process.waitForStarted (10000)) {
      error= QString ("Could not start Codex AppServer: %1")
               .arg (process.errorString ());
      return false;
    }

    QJsonObject response;
    if (!request ("initialize", QJsonObject {
          {"clientInfo", QJsonObject {
            {"name", "athena"}, {"title", "ATHENA"},
            {"version", ATHENA_CODEX_CLIENT_VERSION}}},
          {"capabilities", QJsonObject {{"experimentalApi", false}}}
        }, response, error)) return false;
    sendObject (QJsonObject {
      {"jsonrpc", "2.0"}, {"method", "initialized"},
      {"params", QJsonObject {}}
    });
    return true;
  }

  bool listModels (QJsonArray& models, QString& error) {
    QString cursor;
    do {
      QJsonObject params {{"includeHidden", false}};
      if (!cursor.isEmpty ()) params.insert ("cursor", cursor);
      QJsonObject response;
      if (!request ("model/list", params, response, error)) return false;
      QJsonObject result= response.value ("result").toObject ();
      for (const QJsonValue& value: result.value ("data").toArray ())
        models.append (value);
      cursor= result.value ("nextCursor").toString ();
    } while (!cursor.isEmpty ());
    return true;
  }

  bool startThread (const QString& cwd, const QString& model,
                    const QString& serviceTier,
                    bool webSearchConfigured, bool webSearch,
                    QString& threadId, QString& error) {
    QDir ().mkpath (cwd);
    QString instructions=
      "You are ChatGPT embedded in ATHENA, an IDE for mathematical "
      "knowledge organization and writing. Answer the user's questions "
      "directly. Do not modify files. Use readable plain text and LaTeX "
      "notation when useful. ";
    instructions += webSearch?
      "You may use web search when it is useful, but do not invoke other "
      "tools.": "Do not invoke tools.";
    QJsonObject params {
      {"cwd", QDir (cwd).absolutePath ()},
      {"approvalPolicy", "never"},
      {"sandbox", "read-only"},
      {"ephemeral", true},
      {"baseInstructions", instructions}
    };
    if (!model.isEmpty ()) params.insert ("model", model);
    if (!serviceTier.isEmpty ()) params.insert ("serviceTier", serviceTier);
    if (webSearchConfigured)
      params.insert ("config", QJsonObject {
        {"web_search", webSearch? "live": "disabled"}
      });
    QJsonObject response;
    if (!request ("thread/start", params, response, error)) return false;
    threadId= response.value ("result").toObject ()
                    .value ("thread").toObject ().value ("id").toString ();
    if (threadId.isEmpty ()) {
      error= "Codex AppServer returned no thread id";
      return false;
    }
    return true;
  }

  bool turn (const QString& threadId, const QString& prompt,
             const QString& effort,
             QString& answer, QString& error) {
    QJsonObject params {
      {"threadId", threadId},
      {"input", QJsonArray {QJsonObject {
        {"type", "text"}, {"text", prompt}}}}
    };
    if (!effort.isEmpty ()) params.insert ("effort", effort);
    QJsonObject response;
    if (!request ("turn/start", params, response, error)) return false;
    const QString turnId= response.value ("result").toObject ()
                           .value ("turn").toObject ().value ("id").toString ();
    if (turnId.isEmpty ()) {
      error= "Codex AppServer returned no turn id";
      return false;
    }

    bool completed= false;
    bool failed= false;
    NotificationHandler handler=
      [&] (const QString& method, const QJsonObject& params) {
        if (params.value ("threadId").toString () != threadId) return;
        if (method == "item/agentMessage/delta" &&
            params.value ("turnId").toString () == turnId)
          answer += params.value ("delta").toString ();
        if (method == "turn/completed") {
          QJsonObject turn= params.value ("turn").toObject ();
          if (turn.value ("id").toString () != turnId) return;
          completed= true;
          const QString status= turn.value ("status").toString ();
          if (status != "completed") {
            failed= true;
            QJsonValue detail= turn.value ("error");
            error= detail.isNull ()? QString ("Codex turn ended with %1")
                                      .arg (status):
                                    QString::fromUtf8 (
                                      QJsonDocument (detail.toObject ())
                                        .toJson (QJsonDocument::Compact));
          }
        }
      };
    while (!completed) {
      QJsonObject message;
      if (!readMessage (message, 300000, error)) return false;
      dispatch (message, handler);
    }
    return !failed;
  }

private:
  QProcess process;
  QByteArray inputBuffer;
  qint64 nextId= 1;

  void sendObject (const QJsonObject& object) {
    QByteArray line= QJsonDocument (object).toJson (QJsonDocument::Compact);
    line.append ('\n');
    process.write (line);
    process.waitForBytesWritten (5000);
  }

  void drainStderr () {
    QByteArray bytes= process.readAllStandardError ();
    if (!bytes.isEmpty ()) {
      std::fwrite (bytes.constData (), 1, bytes.size (), stderr);
      std::fflush (stderr);
    }
  }

  bool readMessage (QJsonObject& object, int timeoutMs, QString& error) {
    while (true) {
      qsizetype newline= inputBuffer.indexOf ('\n');
      if (newline >= 0) {
        QByteArray line= inputBuffer.left (newline).trimmed ();
        inputBuffer.remove (0, newline + 1);
        if (line.isEmpty ()) continue;
        QJsonParseError parseError;
        QJsonDocument document= QJsonDocument::fromJson (line, &parseError);
        if (parseError.error != QJsonParseError::NoError ||
            !document.isObject ()) {
          error= QString ("Invalid JSON from Codex AppServer: %1")
                   .arg (parseError.errorString ());
          return false;
        }
        object= document.object ();
        return true;
      }
      if (process.state () == QProcess::NotRunning) {
        drainStderr ();
        error= QString ("Codex AppServer exited with code %1")
                 .arg (process.exitCode ());
        return false;
      }
      if (!process.waitForReadyRead (timeoutMs)) {
        drainStderr ();
        error= process.state () == QProcess::NotRunning?
                 QString ("Codex AppServer exited with code %1")
                   .arg (process.exitCode ()): "Timed out waiting for Codex";
        return false;
      }
      inputBuffer += process.readAllStandardOutput ();
      drainStderr ();
    }
  }

  void dispatch (const QJsonObject& message,
                 const NotificationHandler& handler) {
    const QString method= message.value ("method").toString ();
    if (method.isEmpty ()) return;
    if (message.contains ("id")) {
      sendObject (QJsonObject {
        {"jsonrpc", "2.0"}, {"id", message.value ("id")},
        {"error", QJsonObject {
          {"code", -32601}, {"message", "ATHENA does not expose tools"}}}
      });
      return;
    }
    if (handler) handler (method, message.value ("params").toObject ());
  }

  bool request (const QString& method, const QJsonObject& params,
                QJsonObject& response, QString& error,
                const NotificationHandler& handler= {}) {
    const qint64 id= nextId++;
    sendObject (QJsonObject {
      {"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}
    });
    while (true) {
      QJsonObject message;
      if (!readMessage (message, 30000, error)) return false;
      if (message.value ("id").toVariant ().toLongLong () == id &&
          !message.contains ("method")) {
        if (message.contains ("error")) {
          error= QString::fromUtf8 (
            QJsonDocument (message.value ("error").toObject ())
              .toJson (QJsonDocument::Compact));
          return false;
        }
        response= message;
        return true;
      }
      dispatch (message, handler);
    }
  }
};

int
runSession (AppServer& server, const QString& threadId) {
  emitVerbatim ("OpenAI Codex ChatGPT session\n"
                "Authentication and history use the configured Codex home.");
  QFile input;
  if (!input.open (stdin, QIODevice::ReadOnly)) {
    std::fprintf (stderr, "Cannot read ATHENA session input\n");
    return 1;
  }
  while (!input.atEnd ()) {
    QByteArray line= input.readLine ();
    QString prompt= QString::fromUtf8 (line).trimmed ();
    if (prompt.isEmpty ()) continue;
    QString answer;
    QString error;
    if (server.turn (threadId, prompt, QString (), answer, error))
      emitVerbatim (answer);
    else
      emitVerbatim (QString ("Codex error: %1").arg (error));
  }
  return 0;
}

} // namespace

int
main (int argc, char** argv) {
  QCoreApplication app (argc, argv);
  QCoreApplication::setApplicationName ("athena-codex-bridge");

  QCommandLineParser parser;
  parser.addHelpOption ();
  QCommandLineOption codexOption ("codex", "Codex executable", "path");
  QCommandLineOption homeOption ("codex-home", "Codex home", "path");
  QCommandLineOption cwdOption ("cwd", "Conversation working directory", "path");
  QCommandLineOption oneShotOption ("one-shot", "Run one prompt and exit");
  QCommandLineOption listModelsOption ("list-models", "List available models");
  QCommandLineOption inputOption ("input", "Prompt input file", "path");
  QCommandLineOption outputOption ("output", "Response output file", "path");
  QCommandLineOption modelOption ("model", "Model id", "model");
  QCommandLineOption effortOption ("effort", "Reasoning effort", "effort");
  QCommandLineOption serviceTierOption ("service-tier", "Service tier id",
                                        "tier");
  QCommandLineOption webSearchOption ("web-search", "Allow live web search");
  QCommandLineOption noWebSearchOption ("no-web-search", "Disable web search");
  parser.addOptions ({codexOption, homeOption, cwdOption, oneShotOption,
                      listModelsOption, inputOption, outputOption,
                      modelOption, effortOption, serviceTierOption,
                      webSearchOption, noWebSearchOption});
  parser.process (app);

  QString codex= resolveCodex (parser.value (codexOption));
  if (codex.isEmpty ()) {
    std::fprintf (stderr, "Codex executable was not found\n");
    return 2;
  }
  QString home= parser.value (homeOption);
  if (home.isEmpty ()) home= QDir::home ().filePath (".ATHENA/codex");
  QString cwd= parser.value (cwdOption);
  if (cwd.isEmpty ()) cwd= QDir (home).filePath ("workspace");

  AppServer server;
  QString error;
  if (!server.start (codex, home, error)) {
    std::fprintf (stderr, "%s\n", qPrintable (error));
    return 3;
  }
  if (parser.isSet (listModelsOption)) {
    QJsonArray models;
    if (!server.listModels (models, error)) {
      std::fprintf (stderr, "%s\n", qPrintable (error));
      return 4;
    }
    QByteArray json= QJsonDocument (models).toJson (QJsonDocument::Compact);
    std::fwrite (json.constData (), 1, json.size (), stdout);
    std::fputc ('\n', stdout);
    return 0;
  }
  if (parser.isSet (webSearchOption) && parser.isSet (noWebSearchOption)) {
    std::fprintf (stderr, "--web-search and --no-web-search are exclusive\n");
    return 4;
  }
  QString threadId;
  const bool webSearchConfigured=
    parser.isSet (webSearchOption) || parser.isSet (noWebSearchOption);
  if (!server.startThread (cwd, parser.value (modelOption),
                           parser.value (serviceTierOption), webSearchConfigured,
                           parser.isSet (webSearchOption), threadId, error)) {
    std::fprintf (stderr, "%s\n", qPrintable (error));
    return 4;
  }

  if (!parser.isSet (oneShotOption)) return runSession (server, threadId);

  QFile input (parser.value (inputOption));
  if (!input.open (QIODevice::ReadOnly)) {
    std::fprintf (stderr, "Could not read prompt file\n");
    return 5;
  }
  QString answer;
  if (!server.turn (threadId, QString::fromUtf8 (input.readAll ()),
                    parser.value (effortOption),
                    answer, error)) {
    std::fprintf (stderr, "%s\n", qPrintable (error));
    return 6;
  }
  QFile output (parser.value (outputOption));
  if (!output.open (QIODevice::WriteOnly | QIODevice::Truncate) ||
      output.write (answer.toUtf8 ()) < 0) {
    std::fprintf (stderr, "Could not write response file\n");
    return 7;
  }
  return 0;
}

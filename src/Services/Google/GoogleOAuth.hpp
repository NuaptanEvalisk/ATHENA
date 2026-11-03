/******************************************************************************
* MODULE     : GoogleOAuth.hpp
* DESCRIPTION: OAuth 2.0 desktop authorization for Google services
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef GOOGLEOAUTH_HPP
#define GOOGLEOAUTH_HPP

#include <QString>
#include <functional>

class QNetworkAccessManager;
class QWidget;

class GoogleOAuth {
public:
  using BoolCallback= std::function<void(bool, const QString&)>;
  using TokenCallback= std::function<void(const QString&, const QString&)>;

  static GoogleOAuth& instance ();

  QString clientId () const;
  void    setClientId (const QString& value);
  QString clientSecret () const;
  void    setClientSecret (const QString& value);
  bool    hasRefreshToken () const;
  void    forgetTokens ();

  void authorizeTasks (QWidget* parent, BoolCallback callback);
  void getAccessToken (TokenCallback callback);

private:
  using TokenResult= std::function<void(QString, QString)>;

  GoogleOAuth ();
  QNetworkAccessManager* networkManager ();
  void getAccessTokenOnQt (TokenResult callback);

  struct TokenInfo {
    QString accessToken;
    QString refreshToken;
    qint64   expiresAtSecs= 0;
  };

  QString tokenPath () const;
  TokenInfo loadToken () const;
  bool saveToken (const TokenInfo& token, QString* error= nullptr) const;
  bool accessTokenFresh (const TokenInfo& token) const;
  void exchangeCode (const QString& code, const QString& verifier,
                     const QString& redirectUri, BoolCallback callback);
  void refreshToken (TokenInfo token, TokenResult callback);

  QNetworkAccessManager* manager= nullptr;
};

#endif // GOOGLEOAUTH_HPP

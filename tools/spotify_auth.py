#!/usr/bin/env python3
"""Autoriza o Spotify para o Painel e envia as credenciais para a placa.

Só usa a biblioteca padrão do Python. Uso:
    python3 tools/spotify_auth.py            # autoriza e envia para a placa
    python3 tools/spotify_auth.py --print    # autoriza e mostra os valores para colar no secrets.h

Fluxo: pede Client ID/Secret -> abre o login do Spotify no navegador -> recebe o "code" em
http://127.0.0.1:8888/callback -> troca por tokens -> testa GET /v1/me/player -> envia para a placa
(endereço + PIN da tela de Configurações da placa).
"""
import base64
import datetime
import getpass
import http.server
import json
import secrets
import sys
import urllib.error
import urllib.parse
import urllib.request
import webbrowser

REDIRECT_URI = "http://127.0.0.1:8888/callback"
SCOPES = "user-read-playback-state user-read-currently-playing user-modify-playback-state"


def wait_for_code(expected_state):
    """Sobe um servidor em 127.0.0.1:8888 e espera o Spotify redirecionar o navegador para cá."""
    result = {}

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            url = urllib.parse.urlparse(self.path)
            if url.path != "/callback":
                self.send_response(404)
                self.end_headers()
                return
            params = urllib.parse.parse_qs(url.query)
            result.update({k: v[0] for k, v in params.items()})
            ok = "code" in result and result.get("state") == expected_state
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            msg = "Pronto! Pode fechar esta aba e voltar ao terminal." if ok else "Falhou. Veja o terminal."
            self.wfile.write(f"<h2>{msg}</h2>".encode())

        def log_message(self, *args):
            pass

    with http.server.HTTPServer(("127.0.0.1", 8888), Handler) as server:
        while not result:
            server.handle_request()

    if "error" in result:
        sys.exit(f"O Spotify recusou a autorização: {result['error']}")
    if result.get("state") != expected_state:
        sys.exit("Resposta com 'state' diferente do esperado. Tente de novo.")
    return result["code"]


def post_form(url, data, headers=None):
    req = urllib.request.Request(url, data=urllib.parse.urlencode(data).encode(), headers=headers or {}, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=20) as resp:
            return resp.status, resp.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()


def exchange_code(client_id, client_secret, code):
    basic = base64.b64encode(f"{client_id}:{client_secret}".encode()).decode()
    status, body = post_form(
        "https://accounts.spotify.com/api/token",
        {"grant_type": "authorization_code", "code": code, "redirect_uri": REDIRECT_URI},
        {"Authorization": f"Basic {basic}", "Content-Type": "application/x-www-form-urlencoded"},
    )
    if status != 200:
        sys.exit(f"Falha ao trocar o code por tokens: HTTP {status} {body}")
    return json.loads(body)


def test_player(access_token):
    req = urllib.request.Request("https://api.spotify.com/v1/me/player",
                                 headers={"Authorization": f"Bearer {access_token}"})
    try:
        with urllib.request.urlopen(req, timeout=20) as resp:
            if resp.status == 204:
                print("Teste OK: token funciona (nenhum dispositivo tocando agora).")
            else:
                data = json.loads(resp.read().decode())
                item = data.get("item") or {}
                print(f"Teste OK: tocando agora \"{item.get('name', '?')}\".")
    except urllib.error.HTTPError as e:
        hint = " (sua conta está em User Management do app?)" if e.code == 403 else ""
        sys.exit(f"Teste GET /v1/me/player falhou: HTTP {e.code}{hint} {e.read().decode()}")


def send_to_board(client_id, client_secret, refresh_token, auth_date):
    host = input("Endereço da placa (tela ⚙ da placa) [painel.local]: ").strip() or "painel.local"
    host = host.replace("http://", "").rstrip("/")
    while True:
        pin = input("PIN (tela ⚙ da placa): ").strip()
        status, body = post_form(f"http://{host}/spotify", {
            "pin": pin, "client_id": client_id, "client_secret": client_secret,
            "refresh_token": refresh_token, "auth_date": auth_date,
        })
        if status == 200 and body.strip() == "OK":
            print("Enviado! A placa já está usando as novas credenciais.")
            return
        print(f"A placa respondeu HTTP {status}: {body.strip()}")
        if status != 403:
            sys.exit(1)


def main():
    print_only = "--print" in sys.argv
    client_id = input("Client ID: ").strip()
    client_secret = getpass.getpass("Client Secret (não aparece ao digitar): ").strip()
    if not client_id or not client_secret:
        sys.exit("Client ID e Client Secret são obrigatórios.")

    state = secrets.token_urlsafe(16)
    url = "https://accounts.spotify.com/authorize?" + urllib.parse.urlencode({
        "response_type": "code", "client_id": client_id, "scope": SCOPES,
        "redirect_uri": REDIRECT_URI, "state": state,
    })
    print("Abrindo o login do Spotify no navegador. Se não abrir, copie este endereço:\n" + url)
    webbrowser.open(url)
    code = wait_for_code(state)

    tokens = exchange_code(client_id, client_secret, code)
    test_player(tokens["access_token"])
    auth_date = datetime.date.today().isoformat()

    if print_only:
        print("\nCole no sketches/painel/secrets.h (que fica fora do git):")
        print(f'#define SECRET_SPOTIFY_CLIENT_ID "{client_id}"')
        print(f'#define SECRET_SPOTIFY_CLIENT_SECRET "{client_secret}"')
        print(f'#define SECRET_SPOTIFY_REFRESH_TOKEN "{tokens["refresh_token"]}"')
        print(f'#define SECRET_SPOTIFY_AUTH_DATE "{auth_date}"')
    else:
        send_to_board(client_id, client_secret, tokens["refresh_token"], auth_date)


if __name__ == "__main__":
    main()

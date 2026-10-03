# AES roundtrip test mirroring MainWindow::aesProcess (env-var params, same PS script)
import subprocess, base64, os

SCRIPT = (
    "$ErrorActionPreference='Stop';"
    "[Console]::OutputEncoding=[Text.Encoding]::UTF8;"
    "try{"
    "$pass=$env:TBPASS;"
    "if($env:TBMODE -eq 'enc'){"
    "$raw=[Text.Encoding]::UTF8.GetBytes([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($env:TBDATA)));"
    "$salt=New-Object byte[] 16;$iv=New-Object byte[] 16;"
    "$rng=[Security.Cryptography.RandomNumberGenerator]::Create();"
    "$rng.GetBytes($salt);$rng.GetBytes($iv);"
    "$kdf=New-Object Security.Cryptography.Rfc2898DeriveBytes($pass,$salt,100000);"
    "$aes=[Security.Cryptography.Aes]::Create();$aes.Key=$kdf.GetBytes(32);$aes.IV=$iv;"
    "$enc=$aes.CreateEncryptor();"
    "$ct=$enc.TransformFinalBlock($raw,0,$raw.Length);"
    "$all=New-Object byte[] (32+$ct.Length);"
    "[Array]::Copy($salt,0,$all,0,16);[Array]::Copy($iv,0,$all,16,16);[Array]::Copy($ct,0,$all,32,$ct.Length);"
    "[Convert]::ToBase64String($all)"
    "}else{"
    "$all=[Convert]::FromBase64String($env:TBDATA);"
    "if($all.Length -lt 48){throw 'data too short'}"
    "$salt=[byte[]]$all[0..15];$iv=[byte[]]$all[16..31];"
    "$ct=New-Object byte[] ($all.Length-32);[Array]::Copy($all,32,$ct,0,$ct.Length);"
    "$kdf=New-Object Security.Cryptography.Rfc2898DeriveBytes($pass,$salt,100000);"
    "$aes=[Security.Cryptography.Aes]::Create();$aes.Key=$kdf.GetBytes(32);$aes.IV=$iv;"
    "$dec=$aes.CreateDecryptor();"
    "$pt=$dec.TransformFinalBlock($ct,0,$ct.Length);"
    "[Text.Encoding]::UTF8.GetString($pt)"
    "}}"
    "catch{Write-Output (\"__ERR__\" + $_.Exception.Message)}"
)

def run(mode, pw, data_b64):
    env = dict(os.environ, TBMODE=mode, TBPASS=pw, TBDATA=data_b64)
    r = subprocess.run(['powershell', '-NoProfile', '-Command', SCRIPT],
                       env=env, capture_output=True, timeout=60)
    return r.stdout.decode('utf-8', 'replace').strip()

plain = '机密文本 Secret 123!@# 中文密文测试'
enc_b64 = run('enc', '口令test123', base64.b64encode(plain.encode()).decode())
print('cipher head:', enc_b64[:48], '... len', len(enc_b64))

dec = run('dec', '口令test123', enc_b64)
print('roundtrip:', 'OK' if dec == plain else 'FAIL -> ' + repr(dec))

wrong = run('dec', '错误口令', enc_b64)
print('wrong-pass:', 'REJECTED' if wrong.startswith('__ERR__') or wrong != plain else 'LEAK?!')

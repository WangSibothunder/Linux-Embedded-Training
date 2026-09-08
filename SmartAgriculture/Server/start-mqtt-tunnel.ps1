param(
    [string]$BoardAddress = '192.168.0.232',
    [string]$KnownHosts = (Join-Path $env:USERPROFILE '.ssh\known_hosts')
)

$ErrorActionPreference = 'Stop'
$arguments = @(
    '-N',
    '-o', 'ExitOnForwardFailure=yes',
    '-o', 'ServerAliveInterval=15',
    '-o', 'ServerAliveCountMax=3',
    '-o', 'HostKeyAlgorithms=+ssh-rsa',
    '-o', 'PubkeyAcceptedAlgorithms=+ssh-rsa',
    '-o', 'StrictHostKeyChecking=accept-new',
    '-o', "UserKnownHostsFile=$KnownHosts",
    '-R', '127.0.0.1:1883:127.0.0.1:1883',
    "root@$BoardAddress"
)

& "$env:WINDIR\System32\OpenSSH\ssh.exe" @arguments

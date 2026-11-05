# Windows host side of the phone's USB Ethernet in CDC-NCM mode
# (kernel boot-args sweetusb=2 sweetusbncm=1, patch 0084).
#   powershell -File tools\davinci-usbnet.ps1 [-Log logs\live.txt]
# Waits for the "Sweet iOS (USB NCM)" adapter (Windows' inbox UsbNcm driver),
# gives it 192.168.7.1/24 if it has no address yet (asks for UAC once; the
# phone is 192.168.7.2), then prints the live kernel console the phone streams
# to UDP port 5140, and appends it to -Log if given. Ctrl+C to stop.
param([string]$Log = '')

$mac = '02-00-5E-77-EE-01'
Write-Host "waiting for the phone's USB NCM adapter ($mac)..."
do {
	$nic = Get-NetAdapter -ErrorAction SilentlyContinue | Where-Object { $_.MacAddress -eq $mac }
	if (-not $nic) { Start-Sleep 1 }
} until ($nic)

$ip = Get-NetIPAddress -InterfaceIndex $nic.ifIndex -AddressFamily IPv4 -ErrorAction SilentlyContinue |
	Where-Object { $_.IPAddress -eq '192.168.7.1' }
if (-not $ip) {
	$args = @('interface', 'ipv4', 'set', 'address', "name=`"$($nic.Name)`"", 'static', '192.168.7.1', '255.255.255.0')
	Start-Process netsh -Verb RunAs -Wait -ArgumentList $args | Out-Null
}
Write-Host "$($nic.Name) up: 192.168.7.1 <-> phone 192.168.7.2; live log on udp/5140"

$udp = New-Object System.Net.Sockets.UdpClient 5140
$from = New-Object System.Net.IPEndPoint ([System.Net.IPAddress]::Any, 0)
try {
	while ($true) {
		$text = [System.Text.Encoding]::UTF8.GetString($udp.Receive([ref]$from))
		[Console]::Write($text)
		if ($Log) { Add-Content -Path $Log -Value $text -NoNewline }
	}
} finally {
	$udp.Close()
}

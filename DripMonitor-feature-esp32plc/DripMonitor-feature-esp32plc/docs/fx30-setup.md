# FX30 Gateway Setup

These are the steps to provision a Sierra Wireless FX30 (LTE / Cat-M1 variant,
Legato firmware) as the cellular gateway for a DripMonitor node.  The ESP32 PLC
connects to the FX30 over Ethernet and expects it to provide NAT-routed internet
access at `192.168.13.31/24`.

---

## 1. Physical setup

- Insert the **external mini-SIM (2FF)** into the FX30 SIM tray.
- Attach a **wideband LTE antenna (698–2700 MHz)** to the **cellular SMA** port.
- Attach a **GNSS antenna** to the **GNSS SMA** port (optional but recommended).
- Connect the ESP32 PLC RJ45 port to the FX30 RJ45 port via Cat5e or better.
- Power the FX30 (4.75–32 V DC).

---

## 2. First-time SSH connection

Connect a USB micro-B cable from the FX30 to a laptop.  The FX30 presents a
CDC-Ethernet device (`usb0`, 192.168.2.2):

```sh
ssh root@192.168.2.2
# First login prompts for a new root password — set one and save it.
```

---

## 3. Select the external SIM

By default some FX30 Cat-M1 variants use the internal eSIM (Sierra profile).
Force the external slot:

```sh
cm sim select EXTERNAL_SLOT_1
reboot
# After reboot, verify:
cm sim info   # should show your carrier's ICCID, not "Sierra Wireless"
```

---

## 4. Confirm cellular registration

```sh
cm radio      # wait for "Registered" and a non-zero signal strength
cm data       # verify APN is correct (e.g. iot.1nce.net for 1NCE SIMs)
```

If the APN is wrong:

```sh
cm data apn <your-apn>
```

---

## 5. Bring up the cellular data session

```sh
cm data connect &
sleep 5
cm data info   # should show Interface: rmnet_data0, Connected: yes, an IP, gateway, DNS
ping -c 3 8.8.8.8          # verify FX30 itself has internet
ping -c 3 google.com        # verify DNS works on the FX30
```

> **Note:** `cm data connect` is not persistent.  It uses `modemServices`
> directly and the session can be silently terminated by an inactivity timer.
> For production, replace this with a small Legato app using `dcsService`
> (`le_data` API).  See the Legato documentation at legato.io.

---

## 6. Enable IP forwarding and NAT for the PLC

This lets the PLC (192.168.13.10) route internet traffic through the FX30.

```sh
WAN=rmnet_data0    # substitute if cm data info shows a different interface name
LAN=eth0

sysctl -w net.ipv4.ip_forward=1

iptables -t nat -I POSTROUTING -o $WAN -j MASQUERADE
iptables -I FORWARD -i $WAN -o $LAN -m state --state RELATED,ESTABLISHED -j ACCEPT
iptables -I FORWARD -i $LAN -o $WAN -j ACCEPT
```

Verify end-to-end:

```sh
ping -c 3 192.168.13.10    # should receive replies from the PLC
```

> **Note:** These rules are **not persistent** across FX30 reboots.  To make
> them permanent, follow the Sierra Wireless application note
> _FX30: Network Interfaces and Firewall Rules_ (doc 41111930):
>
> - Add the `*nat` block and FORWARD rules to `/etc/iptables.rules`
>   (before the existing DROP rules in `*filter`).
> - Set `net.ipv4.ip_forward=1` in `/etc/sysctl.conf`.
> - Reload with `/etc/network/if-pre-up.d/iptables` (check for errors before
>   rebooting).
> - Replace `cm data connect` with a Legato app using `le_data` for a
>   persistent, auto-reconnecting session.

---

## 7. Carrier notes

| Carrier | APN | Notes |
|---------|-----|-------|
| 1NCE | `iot.1nce.net` | Roaming SIM, verify activation in 1NCE portal |
| Verizon (MVNO) | carrier-specific | Set via `cm data apn` |

---

## 8. Antenna selection

See project notes in previous design discussions.  Summary for this install:

- **External**: two-cable LTE+GNSS combo puck (e.g. Sierra 6001128 or Taoglas
  MA341), one cable to cellular SMA, one to GNSS SMA.
- **Internal**: Molex 213353 flex (LTE+GNSS) with two U.FL→SMA pigtails.
- Mount LTE antenna vertically (per VDP-style guidance) for best omni
  coverage with unknown azimuth.

---

## Troubleshooting quick-reference

| Symptom | First check |
|---------|-------------|
| `cm radio` shows "not registered, not searching" | `cm sim info` — is `Type: EXTERNAL_SLOT_1`? SIM activated? |
| `cm data connect` → "No interface, cannot proceed" | `ip link` — is `rmnet_data0` present? `cm radio` registered? |
| PLC can't ping FX30 | `ip -4 addr show eth0` on FX30 — is it `192.168.13.31/24`? |
| PLC pings FX30 but no internet | NAT rules applied? `sysctl net.ipv4.ip_forward` = 1? Cellular session up? |
| DNS fails on PLC | `ping 8.8.8.8` from FX30 first — confirm FX30 has internet before blaming NAT |

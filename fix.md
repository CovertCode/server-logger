Excellent question — and this one trips up even experienced admins.
Let’s unpack it cleanly 👇

---

## 🧠 What the error actually means

```
Failed to allocate directory watch: Too many open files
```

This message **does not come from your `stats_logger` service or binary**.
It’s printed by the **`systemctl` client** (the CLI tool), not `systemd` itself.

It means:

> The `systemctl` process tried to create an *inotify watch* on `/etc/systemd/system`
> (to detect changes to unit files in real-time),
> but it hit the kernel limit for the maximum number of file or directory watches allowed per user.

---

## 🧩 What causes it

Each inotify watch consumes a small kernel file descriptor.
Many background processes also use inotify — for example:

| Common cause                                        | Description                                              |
| --------------------------------------------------- | -------------------------------------------------------- |
| **Editors like VS Code / JetBrains IDEs**           | They recursively watch project folders for file changes. |
| **Docker or LXC containers**                        | Systemd inside containers often adds watches for units.  |
| **File sync tools (Dropbox, Syncthing, Nextcloud)** | Continuously watch directories.                          |
| **Many running services / user sessions**           | Each `systemd --user` instance watches config dirs.      |

Eventually, your per-user inotify watch limit (usually 8192) is exceeded.
When that happens, any tool that tries to add a new watch (like `systemctl`) logs this warning.

---

## ⚙️ Check your current limits

Run these commands:

```bash
cat /proc/sys/fs/inotify/max_user_watches
cat /proc/sys/fs/inotify/max_user_instances
```

Typical defaults:

```
8192
128
```

When too low, you’ll see the “Too many open files” warning.

---

## ✅ The Permanent Fix

Raise those limits system-wide.

```bash
# Increase limits
echo "fs.inotify.max_user_watches=524288" | sudo tee -a /etc/sysctl.conf
echo "fs.inotify.max_user_instances=1024" | sudo tee -a /etc/sysctl.conf

# Apply immediately
sudo sysctl -p
```

You can verify the new limits:

```bash
cat /proc/sys/fs/inotify/max_user_watches
cat /proc/sys/fs/inotify/max_user_instances
```

Now the warning will disappear when running:

```bash
sudo systemctl start stats_logger
sudo systemctl daemon-reload
```

---

## 💡 Why your service still works fine

That message happens **before** your service even starts.
It only means `systemctl` couldn’t add a *file system watch* on `/etc/systemd/system`.
It does **not** prevent your service from running, reloading, or restarting.
It’s purely a cosmetic warning caused by the client hitting the kernel watch limit.

---

✅ **TL;DR Summary**

| Problem                                                   | Explanation                                      | Fix                                                                     |
| --------------------------------------------------------- | ------------------------------------------------ | ----------------------------------------------------------------------- |
| “Failed to allocate directory watch: Too many open files” | `systemctl` hit inotify limit                    | raise `fs.inotify.max_user_watches` and `fs.inotify.max_user_instances` |
| Does it affect service?                                   | No                                               | harmless                                                                |
| Permanent fix                                             | Add limits to `/etc/sysctl.conf` and `sysctl -p` |                                                                         |

---

Would you like a quick one-liner script that auto-applies these limits safely (for future systems too)?

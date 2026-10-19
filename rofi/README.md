
# Cutie Colorful Rofi 🌸

https://github.com/user-attachments/assets/aa540a59-0515-40fd-8558-2cdd72b0a828

<p align="center">
  <img width="32.8%" alt="shot-c2" src="https://github.com/user-attachments/assets/0574f64d-6198-4f9a-9579-876a26fc9c84" />
  <img width="32.8%" alt="shot-a6" src="https://github.com/user-attachments/assets/33ba1f09-4c4d-4dad-a34f-866175878578" />
  <img width="32.8%" alt="shot-a1" src="https://github.com/user-attachments/assets/7018c424-3936-4972-84ed-b0623315ce0d" />
</p>

# Installation Guide

## 1. Install Rofi

Install [Rofi](https://github.com/davatorium/rofi) for your system:
- Debian/Ubuntu: `apt install rofi`
- Fedora: `dnf install rofi`
- Arch Linux: `pacman -S rofi`
- OpenSUSE: `zypper install rofi`
- FreeBSD: `pkg install rofi`
- MacOS: `port install rofi`

Or follow the manual installation [here](https://github.com/davatorium/rofi/blob/next/INSTALL.md).

If your installation is successful, you may run rofi with:

```sh
rofi -no-lazy-grab -show drun
```

I'd recommend binding this command to a system hotkey, e.g. "Mod+R" or "Windows+R".

## 2. Install Cutie Colorful Rofi 🌸

Run install script:

```sh
curl -s https://raw.githubusercontent.com/iluvgirlswithglasses/cutie-colorful-rofi/refs/heads/main/install.sh | bash
```

The script will install [astral's uv](https://github.com/astral-sh/uv) if it is not available on your system. Should you want to avoid this behavior, see manual installation guide:

<details>
<summary>Manual installation guide</summary>

```sh
# 1. clone this repository into ~/.config/rofi
git clone --depth 1 https://github.com/iluvgirlswithglasses/cutie-colorful-rofi ~/.config/rofi

# 2. install materialyoucolor using your desired package manager, e.g.:
yay -S python-materialyoucolor
```
</details>

# Customize!

## 1. Generate your ✨ _personalized_ ✨ config with `choose-bg.sh`

Pick a wallpaper for rofi. The script will automatically generate a color palette that matches the wallpaper:

```sh
./choose-bg.sh --path ~/.config/rofi/images/flowers.jpg
```

Should you want the color palette to match another image (e.g. your desktop background), use the `--theme-path` option:

```sh
./choose-bg.sh --path ~/.config/rofi/images/flowers.jpg --theme-path ~/your/desktop/background.png
```

You can run `./choose-bg.sh` with no arguments in order to see the list of available options:

| Flag              | Description                               | Default               |
| ----------------- | ----------------------------------------- | --------------------- |
| `--path`          | Image displayed in the search bar         | _(required)_          |
| `--theme-path`    | Derive palette from a _different_ image   | default to `--path`   |
| `--height`        | Rofi image height (px)                    | `210`                 |
| `--scheme`        | Color scheme variant (see below)          | `vibrant`             |
| `--mode`          | `dark` or `light`                         | `dark`                |

Available schemes: `vibrant` · `tonalspot` · `neutral` · `fidelity` · `content` · `expressive` · `fruitsalad` · `rainbow` · `monochrome`.

## 2. Further tweaking

All `choose-bg.sh` does is fill `config-template.rasi` and write `config.rasi`. Feel free to edit both directly -- change the font, change the icons, or swap colors to your liking.

If you want to change the rofi layout, edit `./themes/ib.rasi`.

## 3. Known issues

- Auto color generation for light theme hasn't been tested.
- The rofi image might take a few hundreds milliseconds to load, and this delay is visible. I don't know how to fix this yet, so I can only recommend resizing the image (e.g. to $700 \times 210$ pixels) so that it loads faster.

# Acknowledgements

This rofi config derives from Aditya Shakya's [Type 7 rofi config](https://github.com/adi1090x/rofi).

Also, the file `./themes/ib.rasi` is named "ib" because the first time I use this config in 2024, the wallpaper was Ib [(dotfiles)](https://github.com/iluvgirlswithglasses/dotfiles). You can also find the light-themed rofi in my showcase in that dotfiles' [cute-light branch](https://github.com/iluvgirlswithglasses/dotfiles/tree/cute-light).


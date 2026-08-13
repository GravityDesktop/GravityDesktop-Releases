"""Generate MSIX visual assets from the project-owned 1024px source icon.

The MSIX resource naming convention is intentional: scale and targetsize are
alternative qualifiers and must never appear on the same resource filename.
"""

from pathlib import Path

from PIL import Image


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "asset-source" / "GravityDesktopIcon.png"
ASSETS = ROOT / "Assets"


def save_square(source: Image.Image, destination: Path, size: int) -> None:
    # The 1024px source is transparent and square.  LANCZOS generates each
    # shell size directly from that original, never from a smaller derivative.
    resized = source.resize((size, size), Image.Resampling.LANCZOS)
    resized.save(destination, format="PNG", optimize=True)


def save_wide(source: Image.Image, destination: Path, width: int, height: int) -> None:
    canvas = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    side = min(width, height)
    icon = source.resize((side, side), Image.Resampling.LANCZOS)
    canvas.alpha_composite(icon, ((width - side) // 2, 0))
    canvas.save(destination, format="PNG", optimize=True)


def main() -> None:
    if not SOURCE.is_file():
        raise SystemExit(f"Missing canonical MSIX icon source: {SOURCE}")
    ASSETS.mkdir(exist_ok=True)
    source = Image.open(SOURCE).convert("RGBA")

    # Unqualified resources are valid scale-100 fallbacks.  All square logo
    # variants are regenerated from the canonical source to avoid the shell
    # choosing a legacy opaque/low-resolution fallback.
    save_square(source, ASSETS / "Square44x44Logo.png", 44)
    save_square(source, ASSETS / "Square150x150Logo.png", 150)
    save_square(source, ASSETS / "StoreLogo.png", 50)
    save_wide(source, ASSETS / "Wide310x150Logo.png", 310, 150)

    for scale in (100, 125, 150, 200, 400):
        save_square(source, ASSETS / f"Square44x44Logo.scale-{scale}.png",
                    round(44 * scale / 100))
        save_square(source, ASSETS / f"Square150x150Logo.scale-{scale}.png",
                    round(150 * scale / 100))
        save_wide(source, ASSETS / f"Wide310x150Logo.scale-{scale}.png",
                  round(310 * scale / 100), round(150 * scale / 100))

    # Target-size variants cover Start/Search/Recent, taskbar, Alt+Tab, and
    # Task View selection without requiring Windows to enlarge a 44px bitmap.
    for target_size in (16, 24, 32, 44, 48, 64, 96, 256):
        save_square(source,
                    ASSETS / f"Square44x44Logo.targetsize-{target_size}.png",
                    target_size)


if __name__ == "__main__":
    main()

from __future__ import annotations

import argparse
import hashlib
import os
import tempfile
from pathlib import Path

_mpl_config = Path(tempfile.gettempdir()) / "athena-data-art-matplotlib"
_mpl_config.mkdir(parents=True, exist_ok=True)
os.environ.setdefault("MPLCONFIGDIR", str(_mpl_config))

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import numpy as np


def _algebraic(params: dict[str, object], x: np.ndarray, y: np.ndarray) -> np.ndarray:
    coeffs = params["coeffs"]
    assert isinstance(coeffs, list)
    return (
        coeffs[0]
        + coeffs[1] * x
        + coeffs[2] * y
        + coeffs[3] * x**2
        + coeffs[4] * y**2
        + coeffs[5] * x * y
        + coeffs[6] * x**3
        + coeffs[7] * y**3
        + coeffs[8] * x * y**2
    )


def _fourier(params: dict[str, object], x: np.ndarray, y: np.ndarray) -> np.ndarray:
    z = np.zeros_like(x)
    terms = params["terms"]
    assert isinstance(terms, list)
    for amp, kx, ky, phase in terms:
        z += amp * np.sin(kx * x + ky * y + phase)
    return z


def _radial(params: dict[str, object], x: np.ndarray, y: np.ndarray) -> np.ndarray:
    coeffs = params["coeffs"]
    assert isinstance(coeffs, list)
    radius = np.sqrt(x**2 + y**2)
    theta = np.arctan2(y, x)
    return coeffs[0] * np.tanh(coeffs[1] * radius) * np.cos(
        coeffs[2] * theta + coeffs[3] * radius + coeffs[4]
    )


def _unit(hex_digest: str, start: int, size: int = 2) -> float:
    return int(hex_digest[start : start + size], 16) / float((16**size) - 1)


def _params(content: str):
    digest = hashlib.sha256(content.encode("utf-8", errors="ignore")).hexdigest()
    generators = [_algebraic, _fourier, _radial]
    generator = generators[int(digest[:2], 16) % len(generators)]
    params: dict[str, object] = {}

    if generator is _algebraic:
        params["coeffs"] = [_unit(digest, 2 + i * 2) * 4.0 - 2.0 for i in range(9)]
    elif generator is _fourier:
        terms = []
        for i in range(5):
            offset = 2 + i * 8
            terms.append(
                (
                    _unit(digest, offset) * 0.55 + 0.18,
                    _unit(digest, offset + 2) * 4.0 - 2.0,
                    _unit(digest, offset + 4) * 4.0 - 2.0,
                    _unit(digest, offset + 6) * 2.0 * np.pi,
                )
            )
        params["terms"] = terms
    else:
        params["coeffs"] = [_unit(digest, 2 + i * 4, 4) * 4.0 - 2.0 for i in range(5)]

    palettes = [
        "viridis",
        "plasma",
        "inferno",
        "magma",
        "cividis",
        "coolwarm",
        "ocean",
        "gist_earth",
        "cubehelix",
    ]
    params["background"] = (
        _unit(digest, 48) * 0.16 + 0.82,
        _unit(digest, 50) * 0.16 + 0.82,
        _unit(digest, 52) * 0.16 + 0.82,
    )
    params["elev"] = int(_unit(digest, 54) * 46.0) + 14
    params["azim"] = int(_unit(digest, 56, 4) * 360.0)
    params["cmap"] = palettes[int(_unit(digest, 60) * len(palettes)) % len(palettes)]
    return generator, params


def generate(content: str, output: Path) -> None:
    generator, params = _params(content)
    output.parent.mkdir(parents=True, exist_ok=True)

    fig = plt.figure(figsize=(19.2, 8.2), dpi=150)
    ax = fig.add_axes([0, 0, 1, 1], projection="3d")
    background = params["background"]
    assert isinstance(background, tuple)
    fig.patch.set_facecolor(background)
    ax.set_facecolor(background)
    ax.set_box_aspect((16, 7, 5))

    grid = np.linspace(-5.0, 5.0, 130)
    x, y = np.meshgrid(grid, grid)
    z = generator(params, x, y)
    ax.plot_surface(
        x,
        y,
        z,
        cmap=str(params["cmap"]),
        rstride=1,
        cstride=1,
        linewidth=0,
        antialiased=True,
        alpha=0.96,
    )
    ax.axis("off")
    ax.view_init(elev=float(params["elev"]), azim=float(params["azim"]))
    plt.savefig(output, dpi=150, pad_inches=0)
    plt.close(fig)


def main() -> int:
    parser = argparse.ArgumentParser(description="Generate an ATHENA DataArt cover PNG.")
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    content = args.input.read_text(encoding="utf-8", errors="ignore")
    if not content.strip():
        content = args.input.name
    generate(content, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

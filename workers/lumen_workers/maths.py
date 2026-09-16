"""Maths worker: solve what you wrote.

Apple's Math Notes in one sentence — write an expression, end it with '=', and the answer appears.
This does the arithmetic half in sympy: evaluate an expression, solve an equation for its unknown,
and remember variables you defined earlier on the same page, so `v = 3` then `2v =` gives 6.

Input is LaTeX (from pix2tex) or plain text; output is both a plain string and LaTeX, so the caller
can render it the same way it renders every other formula.
"""
from __future__ import annotations

import re

import sympy
from sympy.parsing.sympy_parser import (convert_xor, implicit_multiplication_application,
                                        standard_transformations)

from .rpc import serve

_TRANSFORMS = standard_transformations + (implicit_multiplication_application, convert_xor)

# What handwriting recognition leaves behind that sympy will not accept.
_CLEAN = [
    (re.compile(r"\\left|\\right"), ""),
    (re.compile(r"\\cdot|\\times"), "*"),
    (re.compile(r"\\div"), "/"),
    (re.compile(r"[≈≃]"), "="),
    (re.compile(r"[−–—]"), "-"),
    (re.compile(r"×"), "*"),
    (re.compile(r"÷"), "/"),
    (re.compile(r"\s+$"), ""),
]


def _clean(text: str) -> str:
    out = (text or "").strip()
    for pattern, repl in _CLEAN:
        out = pattern.sub(repl, out)
    return out.strip().rstrip("=").strip()


# sympy's LaTeX parser needs antlr, which we do not ship — and school LaTeX is a small language,
# so it is translated here instead. Anything this cannot handle still falls through to sympy.
_LATEX_FUNCS = ("sin", "cos", "tan", "arcsin", "arccos", "arctan", "sinh", "cosh", "tanh",
                "log", "ln", "exp", "min", "max", "gcd", "lcm")


def _braces(text: str, start: int) -> tuple[str, int]:
    """Read a {...} group starting at `start`; returns the contents and the index after it."""
    if start >= len(text) or text[start] != "{":
        return text[start:start + 1], start + 1
    depth, i = 0, start
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1:i], i + 1
        i += 1
    return text[start + 1:], len(text)


def latex_to_text(src: str) -> str:
    out, i = [], 0
    while i < len(src):
        ch = src[i]
        if ch != "\\":
            out.append(ch)
            i += 1
            continue
        j = i + 1
        while j < len(src) and src[j].isalpha():
            j += 1
        name = src[i + 1:j]
        if name in ("frac", "dfrac", "tfrac"):
            num, j = _braces(src, j)
            den, j = _braces(src, j)
            out.append("((" + latex_to_text(num) + ")/(" + latex_to_text(den) + "))")
        elif name == "sqrt":
            arg, j = _braces(src, j)
            out.append("sqrt(" + latex_to_text(arg) + ")")
        elif name in ("cdot", "times"):
            out.append("*")
        elif name == "div":
            out.append("/")
        elif name in ("pi", "tau"):
            out.append(name)
        elif name in _LATEX_FUNCS:
            out.append(name)
        elif name in ("left", "right", "displaystyle", "!", ","):
            pass
        elif name in ("le", "leq"):
            out.append("<=")
        elif name in ("ge", "geq"):
            out.append(">=")
        elif name == "":
            j = i + 2                                # an escaped symbol such as \%
        else:
            out.append(name)                         # a bare name: treat it as a variable
        i = j
    text = "".join(out)
    text = text.replace("^", "**").replace("{", "(").replace("}", ")")
    return re.sub(r"\s+", " ", text).strip()


# sympy pre-defines E, I, N, O, S, Q and beta/gamma/zeta. To a physics student E is energy and I is
# current, so every name that is not a function we understand becomes a plain symbol.
_KNOWN_NAMES = set(_LATEX_FUNCS) | {"sqrt", "pi", "abs", "factorial", "floor", "ceiling", "Abs"}


def _locals_for(text: str) -> dict:
    names = set(re.findall(r"[A-Za-z][A-Za-z0-9_]*", text)) - _KNOWN_NAMES
    return {name: sympy.Symbol(name) for name in names}


def _parse(text: str):
    """Plain maths, with LaTeX translated first (pix2tex writes LaTeX)."""
    candidates = [text]
    if "\\" in text or "{" in text or "^" in text:
        candidates.insert(0, latex_to_text(text))
    errors = []
    for candidate in candidates:
        try:
            return sympy.parse_expr(candidate, local_dict=_locals_for(candidate),
                                    transformations=_TRANSFORMS, evaluate=True)
        except Exception as exc:                     # noqa: BLE001
            errors.append(str(exc))
    raise ValueError("; ".join(errors[-2:]) or "could not read that")


def _pretty(value) -> str:
    """A number a person would write: 6, 9.81, 1/3 — never 9.81000000000000."""
    if isinstance(value, sympy.Basic) and not value.free_symbols:
        try:
            number = complex(value)
        except (TypeError, ValueError):
            return sympy.printing.sstr(value)
        if abs(number.imag) < 1e-12:
            real = number.real
            if abs(real - round(real)) < 1e-9:
                return str(int(round(real)))
            if value.is_Rational and value.q < 1000:
                return sympy.printing.sstr(value)
            return f"{round(real, 6):g}"
    return sympy.printing.sstr(value)


def solve(expression: str = "", context: list | None = None, **_: object) -> dict:
    """expression: what was written (LaTeX or plain). context: earlier lines like 'v = 3'."""
    text = _clean(expression)
    if not text:
        return {"ok": False, "reason": "nothing to solve"}

    subs = {}
    for line in context or []:
        line = _clean(str(line))
        if line.count("=") != 1:
            continue
        left, right = line.split("=")
        try:
            name, value = _parse(left), _parse(right)
        except ValueError:
            continue
        if name.is_Symbol and not value.free_symbols:
            subs[name] = value

    try:
        if text.count("=") == 1:
            left_text, right_text = text.split("=")
            left, right = _parse(left_text), _parse(right_text)
            equation = sympy.Eq(left.subs(subs), right.subs(subs))
            if isinstance(equation, sympy.logic.boolalg.BooleanAtom):   # both sides were numbers
                verdict = bool(equation)
                return {"ok": True, "kind": "check", "result": "true" if verdict else "false",
                        "latex": r"\text{" + ("true" if verdict else "false") + "}", "input": text}
            unknowns = sorted(equation.free_symbols, key=lambda s: s.name)
            if not unknowns:
                verdict = bool(sympy.simplify(equation.lhs - equation.rhs) == 0)
                return {"ok": True, "kind": "check", "result": "true" if verdict else "false",
                        "latex": r"\text{" + ("true" if verdict else "false") + "}", "input": text}
            target = unknowns[0]
            roots = sympy.solve(equation, target, dict=False)
            if not roots:
                return {"ok": False, "reason": f"no solution for {target}"}
            shown = ", ".join(_pretty(r) for r in roots)
            return {"ok": True, "kind": "solve", "variable": target.name, "result": f"{target.name} = {shown}",
                    "latex": sympy.latex(target) + " = " + ", ".join(sympy.latex(r) for r in roots), "input": text}

        value = _parse(text).subs(subs)
        if len(value.free_symbols) > 3:
            return {"ok": False, "reason": "that does not read as maths"}
        simplified = sympy.simplify(value)
        return {"ok": True, "kind": "value", "result": _pretty(simplified),
                "latex": sympy.latex(simplified), "input": text}
    except ValueError as exc:
        return {"ok": False, "reason": str(exc)}
    except Exception as exc:                         # noqa: BLE001 - sympy raises many things
        return {"ok": False, "reason": f"{type(exc).__name__}: {exc}"}


def define(lines: list | None = None, **_: object) -> dict:
    """Which variables a page has defined, so the UI can show them."""
    found = {}
    for line in lines or []:
        text = _clean(str(line))
        if text.count("=") != 1:
            continue
        left, right = text.split("=")
        try:
            name, value = _parse(left), _parse(right)
        except ValueError:
            continue
        if name.is_Symbol and not value.free_symbols:
            found[name.name] = _pretty(value)
    return {"variables": found}


def main() -> int:
    return serve("maths", {"solve": solve, "define": define})


if __name__ == "__main__":
    raise SystemExit(main())

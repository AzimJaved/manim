import os
import hashlib

from constants import TEX_DIR
from constants import TEX_TEXT_TO_REPLACE
from constants import TEX_USE_CTEX
from constants import TEX_FIX_SVG


def tex_hash(expression, template_tex_file):
    id_str = str(expression + template_tex_file)
    hasher = hashlib.sha256()
    hasher.update(id_str.encode())
    # Truncating at 16 bytes for cleanliness
    return hasher.hexdigest()[:16]


def tex_to_svg_file(expression, template_tex_file, **kwargs):
    tex_file = generate_tex_file(expression, template_tex_file, **kwargs)
    dvi_file = tex_to_dvi(tex_file)
    if os.path.basename(template_tex_file) == "code_template.tex":
        remove_rectangles = True
    else:
        remove_rectangles = False
    return dvi_to_svg(dvi_file, remove_rectangles=remove_rectangles)


def generate_tex_file(expression, template_tex_file, **kwargs):
    result = os.path.join(
        TEX_DIR,
        tex_hash(expression, template_tex_file)
    ) + ".tex"
    if not os.path.exists(result):
        print("Writing \"%s\" to %s" % (
            "".join(expression), result
        ))
        with open(template_tex_file, "r") as infile:
            body = infile.read()
            if kwargs is not None and "columns" in kwargs:
                body = body.replace("###COLUMNS###", str(kwargs["columns"]))
            if kwargs is not None and "hsize" in kwargs:
                body = body.replace("###HSIZE###", str(kwargs["hsize"]))
            else:
                body = body.replace("###HSIZE###", "345pt")
            body = body.replace(TEX_TEXT_TO_REPLACE, expression)
        with open(result, "w") as outfile:
            outfile.write(body)
    return result


def get_null():
    if os.name == "nt":
        return "NUL"
    return "/dev/null"


def tex_to_dvi(tex_file):
    result = tex_file.replace(".tex", ".dvi" if not TEX_USE_CTEX else ".xdv")
    if not os.path.exists(result):
        commands = [
            "latex",
            "-interaction=batchmode",
            "-halt-on-error",
            "-output-directory=" + TEX_DIR,
            tex_file,
            ">",
            get_null()
        ] if not TEX_USE_CTEX else [
            "xelatex",
            "-no-pdf",
            "-interaction=batchmode",
            "-halt-on-error",
            "-output-directory=" + TEX_DIR,
            tex_file,
            ">",
            get_null()
        ]
        exit_code = os.system(" ".join(commands))
        if exit_code != 0:
            log_file = tex_file.replace(".tex", ".log")
            raise Exception(
                ("Latex error converting to dvi. " if not TEX_USE_CTEX
                else "Xelatex error converting to xdv. ") +
                "See log output above or the log file: %s" % log_file)
    return result


def dvi_to_svg(dvi_file, regen_if_exists=False, remove_rectangles=False):
    """
    Converts a dvi, which potentially has multiple slides, into a
    directory full of enumerated pngs corresponding with these slides.
    Returns a list of PIL Image objects for these images sorted as they
    where in the dvi
    """
    result = dvi_file.replace(".dvi" if not TEX_USE_CTEX else ".xdv", ".svg")
    if not os.path.exists(result):
        commands = [
            "dvisvgm",
            dvi_file,
            "-n",
            "-v",
            "0",
            "-o",
            result,
            ">",
            get_null()
        ]
        os.system(" ".join(commands))
        
        if TEX_FIX_SVG:
            commands = [
                "cairosvg",
                result,
                "-f",
                "svg",
                "-o",
                result
            ]
            os.system(" ".join(commands))

    if remove_rectangles:
        with open(result, "r+") as f:
            lines = f.readlines()
            f.seek(0)
            for line in lines:
                if "rect" not in line:
                    f.write(line)
            f.truncate()
            f.close()

    return result

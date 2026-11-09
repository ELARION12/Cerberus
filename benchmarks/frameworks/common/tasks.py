"""
Tools and tasks shared by every framework adapter. Standard library only.

Three rounds of growing difficulty, all answerable with the same six tools:

  easy    one tool call
  medium  two tool calls, the second needs the first one's result
  hard    three or four tool calls in a chain, or two results combined

An answer is correct when the expected value appears in it as whole words, so
"The result is 84." and "84" both pass for 84. No partial credit.
"""

import re


def _unquote(text):
    return str(text).strip().strip("'\"").strip()


def _number(value):
    x = float(value)
    return str(int(x)) if x == int(x) else str(x)


def word_count(text):
    return str(len(_unquote(text).split()))


def add(a, b):
    return _number(float(a) + float(b))


def multiply(a, b):
    return _number(float(a) * float(b))


def reverse_text(text):
    return _unquote(text)[::-1]


def to_upper(text):
    return _unquote(text).upper()


def text_length(text):
    return str(len(_unquote(text)))


# Every real tool execution lands here, so a run can report how many tools were
# actually used (a model can sometimes answer an easy task without any).
CALLS = []


def _logged(name, fn):
    def run(*args):
        CALLS.append(name)
        return fn(*args)
    return run


# name, one-line description, parameter style: "text" or "numbers"
TOOLS = [
    (name, desc, style, _logged(name, fn)) for name, desc, style, fn in [
        ("word_count", "Count the words in a text.", "text", word_count),
        ("add", "Add two numbers and return the sum.", "numbers", add),
        ("multiply", "Multiply two numbers and return the product.", "numbers", multiply),
        ("reverse_text", "Reverse the characters of a text.", "text", reverse_text),
        ("to_upper", "Convert a text to uppercase.", "text", to_upper),
        ("text_length", "Count the characters in a text.", "text", text_length),
    ]
]

TOOL_FUNCTIONS = {name: fn for name, _, _, fn in TOOLS}


def run_tool_from_args(name, args):
    """For frameworks that pass one argument string: pull the numbers out when
    the tool takes two, otherwise hand over the text."""
    style = next(s for n, _, s, _ in TOOLS if n == name)
    fn = TOOL_FUNCTIONS[name]
    if style == "numbers":
        nums = re.findall(r"-?\d+(?:\.\d+)?", str(args))
        if len(nums) < 2:
            raise ValueError("need two numbers")
        return fn(nums[0], nums[1])
    return fn(args)


def _t(level, prompt, expected):
    return {"level": level, "prompt": prompt, "expected": expected}


TASKS = [
    # easy: one call
    _t("easy", "How many words are in 'the quick brown fox'?", "4"),
    _t("easy", "Count the words in: one two three four five six", "6"),
    _t("easy", "What is 17 plus 25?", "42"),
    _t("easy", "Add 120 and 305.", "425"),
    _t("easy", "Reverse the text 'stressed'.", "desserts"),
    _t("easy", "Reverse the text 'level up'.", "pu level"),
    _t("easy", "Convert 'hello world' to uppercase.", "HELLO WORLD"),
    _t("easy", "How many characters are in 'edge devices'?", "12"),
    # medium: two calls, the second uses the first result
    _t("medium", "Reverse the word 'hello' and then convert the result to uppercase.", "OLLEH"),
    _t("medium", "Count the words in 'a b c d' and then add 10 to that number.", "14"),
    _t("medium", "Add 17 and 25, then multiply the result by 2.", "84"),
    _t("medium", "Reverse the word 'robot', then count how many characters the result has.", "5"),
    _t("medium", "Multiply 6 by 7, then reverse the digits of the result.", "24"),
    _t("medium", "Reverse the word 'abc' and then convert the result to uppercase.", "CBA"),
    _t("medium", "How many characters are in the uppercase version of 'pi zero'?", "7"),
    _t("medium", "Add 100 and 23, then count how many characters the result has.", "3"),
    # hard: three or four calls
    _t("hard", "Count the words in 'the quick brown fox jumps', multiply that count by 7, then add 5.", "40"),
    _t("hard", "Reverse the word 'stressed', convert it to uppercase, then count its characters.", "8"),
    _t("hard", "Add 12 and 30, multiply the result by 3, then reverse the digits of that result.", "621"),
    _t("hard", "Count the words in 'to be or not to be', then add 4 and 9, then multiply those two results together.", "78"),
    _t("hard", "Convert 'edge' to uppercase, reverse it, count its characters, then add 100.", "104"),
    _t("hard", "Multiply 12 by 12, add 6 to the result, then count how many characters that number has.", "3"),
]

LEVELS = ("easy", "medium", "hard")


def _norm(text):
    return " ".join(re.findall(r"[a-z0-9]+", str(text).lower()))


def is_correct(answer, expected):
    return f" {_norm(expected)} " in f" {_norm(answer)} "


def selftest():
    """The expected values must follow from the tools, or the benchmark lies."""
    f = TOOL_FUNCTIONS
    assert f["word_count"]("the quick brown fox") == "4"
    assert f["text_length"]("edge devices") == "12"
    assert f["to_upper"](f["reverse_text"]("hello")) == "OLLEH"
    assert f["add"](f["word_count"]("a b c d"), 10) == "14"
    assert f["multiply"](f["add"](17, 25), 2) == "84"
    assert f["text_length"](f["reverse_text"]("robot")) == "5"
    assert f["reverse_text"](f["multiply"](6, 7)) == "24"
    assert f["to_upper"](f["reverse_text"]("abc")) == "CBA"
    assert f["text_length"](f["to_upper"]("pi zero")) == "7"
    assert f["text_length"](f["add"](100, 23)) == "3"
    assert f["add"](f["multiply"](f["word_count"]("the quick brown fox jumps"), 7), 5) == "40"
    assert f["text_length"](f["to_upper"](f["reverse_text"]("stressed"))) == "8"
    assert f["reverse_text"](f["multiply"](f["add"](12, 30), 3)) == "621"
    assert f["multiply"](f["word_count"]("to be or not to be"), f["add"](4, 9)) == "78"
    assert f["add"](f["text_length"](f["reverse_text"](f["to_upper"]("edge"))), 100) == "104"
    assert f["text_length"](f["add"](f["multiply"](12, 12), 6)) == "3"
    CALLS.clear()
    for t in TASKS:
        assert is_correct(t["expected"], t["expected"])
    assert is_correct("The result is 84.", "84") and not is_correct("The result is 840.", "84")
    assert len(TASKS) == 22 and {t["level"] for t in TASKS} == set(LEVELS)


if __name__ == "__main__":
    selftest()
    print("tasks ok:", {lv: sum(t["level"] == lv for t in TASKS) for lv in LEVELS})

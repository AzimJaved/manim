import os


def play_chord(*nums):
    commands = [
        "play",
        "-n",
        "-c1",
        "--no-show-progress",
        "synth",
    ] + [
        "sin %-" + str(num)
        for num in nums
    ] + [
        "fade h 0.5 1 0.5",
        "> /dev/null"
    ]
    try:
        os.system(" ".join(commands))
    except:
        pass


def play_error_sound():
    # play_chord(11, 8, 6, 1)
    print("====================")
    print("Encountered an error")
    print("====================")
    return 1


def play_finish_sound():
    # play_chord(12, 9, 5, 2)
    print("======================")
    print("Finished without error")
    print("======================")
    return 0

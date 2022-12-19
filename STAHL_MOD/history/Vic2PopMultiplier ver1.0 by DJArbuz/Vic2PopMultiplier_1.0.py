Multiplier = float(input("Input multiplier --> "))
import os
from fnmatch import fnmatch
pattern = "*.txt"
for path_provinces, subdirs_provinces, files_provinces in os.walk("FilesToModify"):
    if len(files_provinces) == 0:
        continue
    for name_provinces in files_provinces:
        if fnmatch(name_provinces, pattern):
            with open(path_provinces + "\\" + name_provinces, "r") as fp:
                data = fp.readlines()
                print(data)
            with open(path_provinces + "\\" + name_provinces, "w") as fp:
                fp.close()
            with open(path_provinces + "\\" + name_provinces, "r+") as fp:
                for line in data:
                    if not (line.__contains__("size = ")):
                        fp.write(line)
                    if line.__contains__("size = "):
                        CurrentPop = line
                        SplitAt = 9
                        SizeDef, PopAmount = CurrentPop[:SplitAt], CurrentPop[SplitAt:]
                        PopAmount = int(PopAmount) * Multiplier
                        PopAmount = int(PopAmount // 1)
                        CurrentPop = SizeDef + str(PopAmount)
                        line = CurrentPop + "\n"
                        print(line)
                        fp.write(line)


import os

def patchRolls(fp, outfile):
    data = ''
    with open(fp, 'rb') as f:
        data = f.read()
    for key in PATCHES:
        for k, v in PATCHES[key].items():
            if key == "SUBROUTINE_SMALL" and Small_Roll is False:
                continue
            elif key == "SUBROUTINE_BIG" and Small_Roll is True:
                continue
            data = data.replace(bytearray.fromhex(k), bytearray.fromhex(v))
    with open(outfile, 'wb') as o:
        o.write(data)

def fileCheck(fp):
    data = ''
    with open(fp, 'rb') as f:
        data = f.read()
    for key in PATCHES:
        for def_val in PATCHES[key]:
            if key == "SUBROUTINE_SMALL" and Small_Roll is False:
                continue
            elif key == "SUBROUTINE_BIG" and Small_Roll is True:
                continue
            if data.find(bytearray.fromhex(def_val)) == -1:
                return False
    return True
print("This script will change the maximum and minimum combat rolls by patching the exe.\nInput new maximum and minimum rolls.\nA new patched exe will be created afterwards. The checksum will change.\nInput a range of 1 (like 5-5) to get a constant roll.\nWarning: Setting rolls higher than intended may cause wierd stuff to happen or crashes.")
MinRoll = input("Enter the minimum roll:")
MaxRoll = input("Enter the maxiumum roll:")

try:
    MaxRoll = int(MaxRoll)
    MinRoll = int(MinRoll)
except ValueError:
    input("Invalid Input! Press any key to quit.")
    exit()
if 0 > MaxRoll or 0 > MinRoll:
    input("Invalid Input! Cannot use negative numbers! Press any key to quit.")
    exit()



ModuloFactor = (MaxRoll - MinRoll) + 1

ModuloHex = (ModuloFactor).to_bytes(4, byteorder='little').hex()

if 128 > MinRoll:
    MinRollHex = "83 C2" + (MinRoll).to_bytes(1, byteorder='little').hex()
    Small_Roll = True
else:
    MinRollHex = "81 C2 " + (MinRoll).to_bytes(4, byteorder='little').hex()
    Small_Roll = False


PATCHES = {
    'CALL_SUB': {
        # Create Call to subroutine to exectute patch
        'E8 B9 AE 41 00 99 B9 0A 00 00 00 F7 F9': 'E8 B9 AE 41 00 99 E8 80 C6 6E 00 90 90',
        'E8 A3 AE 41 00 99 B9 0A 00 00 00 F7 F9': 'E8 A3 AE 41 00 99 E8 6A C6 6E 00 90 90',
        'E8 35 AE 41 00 99 B9 0A 00 00 00 F7 F9': 'E8 35 AE 41 00 99 E8 FC C5 6E 00 90 90',
        'E8 1F AE 41 00 99 B9 0A 00 00 00 F7 F9': 'E8 1F AE 41 00 99 E8 E6 C5 6E 00 90 90'
    },
    'SUBROUTINE_SMALL': {
        # The instructions for the patch in the subroutine for small roll
        'F2 00 E9 96 5C E9 FF 00 00 00 00 00 00 00 00 00 00 00 00 00 00': 'F2 00 E9 96 5C E9 FF 00 00 00 B9 {} F7 F9 {} C3'.format(ModuloHex, MinRollHex)
    },
    'SUBROUTINE_BIG': {
        # The instructions for the patch in the subroutine for big roll
        'F2 00 E9 96 5C E9 FF 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00': 'F2 00 E9 96 5C E9 FF 00 00 00 B9 {} F7 F9 {} C3'.format(ModuloHex, MinRollHex)
    }
}

if not fileCheck(os.getcwd() + "\\v2game.exe"):
    input("Valid v2game.exe not found! Press enter to quit.")
    exit()
if MinRoll > MaxRoll:
    input("Minimum Roll cannot be larger than maximum roll! Press enter to quit.")
    exit()


patchRolls(os.getcwd() + "\\v2game.exe", "v2game" + str(MinRoll) + "to" + str(MaxRoll) + ".exe")

input("Sucess! A new executable has been created. Press enter to quit.")


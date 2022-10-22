import sys,os
from PIL import Image
from collections import defaultdict

print("Finding sea provinces...")

disallowedcharacters = ["\n", "#", " ", "\r"]

#Get sea provinces from default.map
with open("./map/default.map") as fp:
    contents = fp.read()
    replacedcontents = contents.replace("}", "{")
    sea_provinces = replacedcontents.split("{")
    del sea_provinces[2:]
    del sea_provinces[0]
    sea_provinces2 = sea_provinces[0].strip()
    sea_province_list = sea_provinces2.split()

print("Finding land provinces...")
land_provinces = []
#Get land provinces from region.txt
with open("./map/region.txt") as fp:
    for line in fp:
        if line[0] in disallowedcharacters:
            continue
        else:
            line_no_spaces = line.strip()
            line_replace_brackets = line_no_spaces.replace("}", "{")
            line_split = line_replace_brackets.split("{")
            land_provinces_spaces = line_split[1]
            land_provinces_str = land_provinces_spaces.strip()
            land_provinces_list = land_provinces_str.split()
            for x in land_provinces_list:
                land_provinces.append(x)

list_of_provinces_ID = defaultdict(list)

print("Finding province IDs...")
allowedcharacters = "0123456789"
#Get province IDs and their RGBs from definition.csv, and index if they are land or sea provinces in dict
with open("./map/definition.csv") as fp:
    for line in fp:
        if line[0] in allowedcharacters:
            raw_province = line.replace(".", "")
            tempprovince = raw_province.split(";")
            del tempprovince[4:]
            prov_ID = tempprovince[0]
            if prov_ID in land_provinces:
                #is land province or not
                is_land = "1"
            elif prov_ID in sea_province_list:
                is_land = "0"
            else:
                continue
            red_def = int(tempprovince[1])
            green_def = int(tempprovince[2])
            blue_def = int(tempprovince[3])
            RGB_definition = tuple((red_def, green_def, blue_def))
            list_of_provinces_ID[RGB_definition].append(prov_ID)
            list_of_provinces_ID[RGB_definition].append(is_land)
print("Fetching BMP data...")
src=os.getcwd()
#Getting the position and color of each pixel in provinces.bmp
print(src)
path=src+"\\map/provinces.bmp"
img=Image.open(path,"r")
width, height=img.size
my_list = list((img.getdata()))
RGB_neighbors = defaultdict(list)
list_of_provinces_RGB = []
#Use img.getdata to fetch BMP data, and use the BMP IDs to locate each pixel on the map and their neighbors. Check if they are sea provs
#Determine if their neighbors are diffrent RGB, land provinces
for i in range(0, len(my_list)):
    RGB = my_list[i]
    try:
        if RGB in list_of_provinces_ID.keys() and list_of_provinces_ID[RGB][1] == "0":
            try:
                if RGB != my_list[i+1] and i != width * height and list_of_provinces_ID[my_list[i+1]][1] == "1":
                    if my_list[i+1] not in RGB_neighbors[RGB]:
                        RGB_neighbors[RGB].append(my_list[i+1])
            except IndexError:
                pass
            try:
                if RGB != my_list[i-1] and i != 0 and list_of_provinces_ID[my_list[i-1]][1] == "1":
                    if my_list[i-1] not in RGB_neighbors[RGB]:
                        RGB_neighbors[RGB].append(my_list[i-1])
            except IndexError:
                pass
            try:
                if RGB != my_list[i+width] and i <= (width * height) - width and list_of_provinces_ID[my_list[i+width]][1] == "1":
                    if my_list[i+width] not in RGB_neighbors[RGB]:
                        RGB_neighbors[RGB].append(my_list[i+width])
            except IndexError:
                pass
            try:
                if RGB != my_list[i-width] and i >= width and list_of_provinces_ID[my_list[i-width]][1] == "1":
                    if my_list[i-width] not in RGB_neighbors[RGB]:
                        RGB_neighbors[RGB].append(my_list[i-width])
            except IndexError:
                pass
    except IndexError:
        pass
#Write data to adjacencies.csv by looking into dicts with the key value (RGB) to locate the prov ID
print("Writing adjacencies...")
with open("./map/adjacencies.csv", "a") as adjacencies:
    adjacencies.write("\n#### Sea Adjacencies ####\n")
    for line in RGB_neighbors.items():
        land_line = line[1:]
        sea_line = line[0]
        count = 0
        for land_tiles in land_line[0+count]:
            count += 1
            adjacencies.write(list_of_provinces_ID[sea_line][0] + ";" + list_of_provinces_ID[land_tiles][0] + ";sea;0;0;\n")
input('Press ENTER to exit')
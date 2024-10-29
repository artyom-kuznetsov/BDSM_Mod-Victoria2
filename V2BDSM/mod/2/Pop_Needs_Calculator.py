from time import sleep
from string import ascii_letters
from re import sub
from re import match

with open('Pop_Needs_Calculator_output.txt', 'w') as file:
    file.write("")

alphabet = ascii_letters
goodspath = "common\\goods.txt"

def extract_goods(goodspath):
    good_name = None
    goods_list = {}
    with open(goodspath, 'r') as fp:
        lines = fp.readlines()
        for line in lines:
            line = line.strip()
            if match(r'^\w+\s*=\s*{', line):
                        good_name = sub(r'\s*=\s*{', '', line)
            if good_name and match(r'cost\s*=\s*\d+(\.\d+)?', line):
                price = sub(r'cost\s*=\s*', '', line)
                goods_list[good_name] = float(price)
    return goods_list

goods_list = extract_goods(goodspath)

for each in goods_list:
    print(each)

life_needs_start = False
everyday_needs_start = False
luxury_needs_start = False

with open('Pop_Needs_Calculator_output.txt', 'a') as file:
    file.write(str(goods_list))
    file.write("\n\n")

popfiles = int(13)
while popfiles != 0:
    total_life_needs_price = float(0)
    total_everyday_needs_price = float(0)
    total_luxury_needs_price = float(0)

    if popfiles == 13: 
        print ("opening aristocrats.txt")
        filepath = "poptypes\\aristocrats.txt"
        popfiles = popfiles - 1
        with open('Pop_Needs_Calculator_output.txt', 'a') as file:
            file.write("Aristocrats:")
    elif popfiles == 12:
        print ("opening artisans.txt")
        filepath = "poptypes\\artisans.txt"
        popfiles = popfiles - 1
        with open('Pop_Needs_Calculator_output.txt', 'a') as file:
            file.write("Artisans:")
    elif popfiles == 11:
        print ("opening bureaucrats.txt")
        filepath = "poptypes\\bureaucrats.txt"
        popfiles = popfiles - 1
        with open('Pop_Needs_Calculator_output.txt', 'a') as file:
            file.write("Bureaucrats:")
    elif popfiles == 10:
        print ("opening capitalists.txt")
        filepath = "poptypes\\capitalists.txt"
        popfiles = popfiles - 1
        with open('Pop_Needs_Calculator_output.txt', 'a') as file:
            file.write("Capitalists:")
    elif popfiles == 9:
        print ("opening clergymen.txt")
        filepath = "poptypes\\clergymen.txt"
        popfiles = popfiles - 1
        with open('Pop_Needs_Calculator_output.txt', 'a') as file:
            file.write("Clergymen:")
    elif popfiles == 8:
        print ("opening clerks.txt")
        filepath = "poptypes\\clerks.txt"
        popfiles = popfiles - 1
        with open('Pop_Needs_Calculator_output.txt', 'a') as file:
            file.write("Clerks:")
    elif popfiles == 7:
        print ("opening craftsmen.txt")
        filepath = "poptypes\\craftsmen.txt"
        popfiles = popfiles - 1
        with open('Pop_Needs_Calculator_output.txt', 'a') as file:
            file.write("Craftsmen:")
    elif popfiles == 6:
        print ("opening farmers.txt")
        filepath = "poptypes\\farmers.txt"
        popfiles = popfiles - 1
        with open('Pop_Needs_Calculator_output.txt', 'a') as file:
            file.write("Farmers:")
    elif popfiles == 5:
        print ("opening labourers.txt")
        filepath = "poptypes\\labourers.txt"
        popfiles = popfiles - 1
        with open('Pop_Needs_Calculator_output.txt', 'a') as file:
            file.write("Labourers:")
    elif popfiles == 4:
        print ("opening officers.txt")
        filepath = "poptypes\\officers.txt"
        popfiles = popfiles - 1
        with open('Pop_Needs_Calculator_output.txt', 'a') as file:
            file.write("Officers:")
    elif popfiles == 3:
        print ("opening slaves.txt")
        filepath = "poptypes\\slaves.txt"
        popfiles = popfiles - 1
        with open('Pop_Needs_Calculator_output.txt', 'a') as file:
            file.write("Slaves:")
    elif popfiles == 2:
        print ("opening soldiers.txt")
        filepath = "poptypes\\soldiers.txt"
        popfiles = popfiles - 1
        with open('Pop_Needs_Calculator_output.txt', 'a') as file:
            file.write("Soldiers:")
    elif popfiles == 1:
        print ("opening zbankers.txt")
        filepath = "poptypes\\zbankers.txt"
        popfiles = popfiles - 1
        with open('Pop_Needs_Calculator_output.txt', 'a') as file:
            file.write("Bankers:")
    with open(filepath) as fp:
        lines = fp.readlines()
        
        for line in lines:
            line = line.split('#')[0]
            if "life_needs = {" in line:
                print ("found life_needs definition start")
                life_needs_start = True
            if life_needs_start == True:
                if "}" in line:
                    print ("found life_needs definition end")
                    life_needs_start = False
                else:
                    checkgood = sub(r'[^a-zA-Z_]', '', line)
                    if checkgood in goods_list:

                        print("found a good", checkgood,"in line:\n    ", line)
                        good_price = goods_list[checkgood]
                        good_amount = float(sub(r'[^0-9.]', '', line))
                        total_life_needs_price = total_life_needs_price + good_amount * good_price


            if "everyday_needs = {" in line:
                print ("found everyday_needs definition start")
                everyday_needs_start = True
            if everyday_needs_start == True:
                if "}" in line:
                    print ("found everyday_needs definition end")
                    everyday_needs_start = False
                else:
                    checkgood = sub(r'[^a-zA-Z_]', '', line)
                    if checkgood in goods_list:

                        print("found a good", checkgood,"in line:\n    ", line)
                        good_price = goods_list[checkgood]
                        
                        #with open('Pop_Needs_Calculator_output.txt', 'a') as file:
                            #file.write("\n        Good Price: ")
                            #file.write(str(good_price))
                            #file.write("\n        Good Definition: ")
                            #file.write(str(checkgood))
                        good_amount = float(sub(r'[^0-9.]', '', line))

                        #with open('Pop_Needs_Calculator_output.txt', 'a') as file:
                            #file.write("\n        Good Amount: ")
                            #file.write(str(good_amount))
                        
                        total_everyday_needs_price = total_everyday_needs_price + good_amount * good_price
                        #with open('Pop_Needs_Calculator_output.txt', 'a') as file:
                            #file.write("\n    Everyday needs price: ")
                            #file.write(str(total_everyday_needs_price))

            if "luxury_needs = {" in line:
                print ("found luxury_needs definition start")
                luxury_needs_start = True
            if luxury_needs_start == True:
                if "}" in line:
                    print ("found luxury_needs definition end")
                    luxury_needs_start = False
                else:
                    checkgood = sub(r'[^a-zA-Z_]', '', line)
                    if checkgood in goods_list:

                        print("found a good", checkgood,"in line:\n    ", line)
                        good_price = goods_list[checkgood]
                        line = line.split('#')[0]
                        good_amount = float(sub(r'[^0-9.]', '', line))
                        total_luxury_needs_price = total_luxury_needs_price + good_amount * good_price
            
    print ("    Total Life Needs Price:\n        ", total_life_needs_price)
    print ("    Total Everyday Needs Price:\n        ", total_everyday_needs_price)
    print ("    Total Luxury Needs Price:\n        ", total_luxury_needs_price)
    with open('Pop_Needs_Calculator_output.txt', 'a') as file:
        file.write("\n    Total Life Needs Price:\n        ")
        file.write(str(total_life_needs_price))
        file.write("\n    Total Everyday Needs Price:\n        ")
        file.write(str(total_everyday_needs_price))
        file.write("\n    Total Luxury Needs Price:\n       ")
        file.write(str(total_luxury_needs_price))
        file.write("\n\n")
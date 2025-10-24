# Prerequisites

You need [python](https://apps.microsoft.com/detail/9pnrbtzxmb4z?) installed first and the python library PIL.

PIL is installed in the Windows terminal after Python is installed.
```
pip install pillow
```
# Downloading

Right click and save the raw file: [Victoria 2 GUI war analyzer.py](</Victoria 2 GUI war analyzer.py?raw=true>)

# Running the app

It should be as easy as double clicking the file. After starting the app you will be prompted to select the installation folder for Victoria 2, this is because this app comes without any graphical elements and only uses what comes with the game.

## Settings

Choose your save file and the corresponding folder if the save is from a MOD. This is to load in custom flags, country names and such. After that load the save file. This may take a few seconds. It doesn't have perfect mod support in that mods that make big changes to the graphics especially black backgrounds.

![Settings](/images/settings.png)

## Show wars

You will be presented with a list of the wars from the save file, because of instability issues it only shows 200 at a time. Wars highlighted in gold are the wars where the player is involved.
The overview of the war here shows the flags of the countries involved. The icon of the original casus belli of the war, the total number of soldiers on both sides that have been involved in battles. A war score calculated based on battles won and casualties, war scores of old wars are not stored in the save file so this is a made up calculation that does indicate 100% what side actually won the war.

![Show wars](/images/show_wars1.png)

You can select one or more countries from the list to filter out only to show wars they're involved in. The country list is sorted alphabetically after TAG not country name. Wars highlighted in grey are still active in the save.

![Show wars2](/images/show_wars2.png)

## War Details

Here we see the name of the war, original reason for the war. There is sadly not way in old games to find all the war goals that were added in a war. The dates of the war. The two original attackers and defenders shown in large, this is not automatically the same as who was war leader. A list of battles. Here the generation of big ship in the battle is represented.

![War Details](/images/war_details.png)

If one or both nations are Great Powers at the moment in the save file they will be represented by another ring around the flag. The number on each battle indicates the inital army or fleet size in that battle. The green/red bar how many who survived.

![War Details2](/images/war_details2.png)

## Battle popup

Here we can see a bit more information about each battle. In the save file we sadly don't get what type of unit died in battle, only the total.

![Naval battle](/images/naval_battle.png)

Same for land battless.

![Land battle](/images/land_battle.png)

If a general or admiral has died it's not possible to see what + or - attributes they have or what leader icon they used to have. But they will get a random icon that matches their countrys culture.

![Land battle2](/images/land_battle2.png)

# Bugs and crashes

It's pretty stable but not perfect, I'm not a coder so I don't know what I'm doing. Feel free to find improvements. Crashes most often happens if you watch too many wars without closing the app in between.

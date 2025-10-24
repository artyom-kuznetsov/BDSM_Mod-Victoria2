"""Victoria 2 War Analyzer - Enhanced GUI tool for analyzing wars from Victoria 2 save files."""

import os
import re
import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Set, Tuple
import tkinter as tk
from tkinter import filedialog, messagebox
from PIL import Image, ImageTk, ImageDraw, ImageFont


# ============================================================================
# CONSTANTS
# ============================================================================

WINDOW_WIDTH = 684
WINDOW_HEIGHT = 641

# War list dimensions
WAR_LIST_X = 43
WAR_LIST_Y = 60
WAR_LIST_WIDTH = 620
WAR_LIST_HEIGHT = 224
SCROLLBAR_WIDTH = 16

# Filter list dimensions
FILTER_LIST_X = 27
FILTER_LIST_Y = WAR_LIST_Y + WAR_LIST_HEIGHT + 44
FILTER_LIST_WIDTH = 639
FILTER_LIST_HEIGHT = 287

# Sort button dimensions
SORT_BUTTON_X = FILTER_LIST_X - 1
SORT_BUTTON_Y = WAR_LIST_Y + WAR_LIST_HEIGHT + 23
SORT_BUTTON_WIDTH = 166
SORT_BUTTON_HEIGHT = 25

# Additional sort buttons
SORT_BUTTON_42_X = SORT_BUTTON_X + SORT_BUTTON_WIDTH
SORT_BUTTON_42_Y = SORT_BUTTON_Y
SORT_BUTTON_42_WIDTH = 42
SORT_BUTTON_42_HEIGHT = 20

SORT_BUTTON_24_X = SORT_BUTTON_42_X + SORT_BUTTON_42_WIDTH
SORT_BUTTON_24_Y = SORT_BUTTON_Y
SORT_BUTTON_24_WIDTH = 24
SORT_BUTTON_24_HEIGHT = 20

# Flag dimensions
FLAG_WIDTH = 24
FLAG_HEIGHT = 17

# UI Colors
BG_COLOR = "#e0ded5"

# ============================================================================
# DATA CLASSES
# ============================================================================

@dataclass
class Battle:
    """Represents a single battle within a war."""
    name: str
    location: Optional[str]
    attacker: Dict
    defender: Dict
    result: Optional[bool]


@dataclass
class WarGoal:
    """Represents a single war goal with all its details."""
    description: str
    casus_belli: str
    actor: str
    receiver: str
    state_province_id: Optional[str] = None


@dataclass
class War:
    """Represents a war with all its participants and battles."""
    name: str
    attackers: List[str]
    defenders: List[str]
    goals: List[WarGoal]
    battles: List[Battle]
    original_attacker: Optional[str]
    original_defender: Optional[str]
    start_date: Optional[str] = None
    end_date: Optional[str] = None
    is_active: bool = False

@dataclass
class WarStatistics:
    """Enhanced war statistics"""
    total_battles: int = 0
    attacker_wins: int = 0
    defender_wins: int = 0
    total_casualties: Dict[str, int] = field(default_factory=dict)
    war_score_estimate: float = 0.0
    duration_estimate: Optional[str] = None

@dataclass
class AppConfig:
    """Application configuration with multiple mod support."""
    recent_files: List[str] = field(default_factory=list)
    window_position: Tuple[int, int] = (100, 100)
    default_mods: List[str] = field(default_factory=list)  # Changed from default_mod to default_mods
    auto_load_last: bool = False
    last_mods: List[str] = field(default_factory=list)  # Changed from last_mod to last_mods
    
    @classmethod
    def load(cls, path: str = None):
        """Load configuration from JSON file with migration from single-mod format."""
        # Use the same directory as the Python script
        if path is None:
            script_dir = os.path.dirname(os.path.abspath(__file__))
            path = os.path.join(script_dir, "war_analyzer_config.json")
        
        try:
            if os.path.exists(path):
                with open(path, 'r') as f:
                    data = json.load(f)
                    
                    # Migrate from old single-mod format to new multiple mods format
                    data = cls._migrate_from_single_mod(data)
                    
                    # Ensure all fields exist with proper defaults
                    if 'recent_files' not in data:
                        data['recent_files'] = []
                    if 'window_position' not in data:
                        data['window_position'] = (100, 100)
                    if 'default_mods' not in data:
                        data['default_mods'] = []
                    if 'auto_load_last' not in data:
                        data['auto_load_last'] = False
                    if 'last_mods' not in data:
                        data['last_mods'] = []
                        
                    return cls(**data)
        except Exception as e:
            print(f"Error loading config: {e}")
            # Silently return default config if loading fails
            pass
        return cls()
    
    @classmethod
    def _migrate_from_single_mod(cls, data: Dict) -> Dict:
        """Migrate from old single-mod format to new multiple mods format."""
        # Check if we have old single-mod fields that need migration
        if 'default_mod' in data and 'default_mods' not in data:
            # Migrate default_mod to default_mods
            if data['default_mod'] and data['default_mod'] != "None":
                data['default_mods'] = [data['default_mod']]
            else:
                data['default_mods'] = []
            # Remove the old field to avoid confusion
            del data['default_mod']
        
        if 'last_mod' in data and 'last_mods' not in data:
            # Migrate last_mod to last_mods
            if data['last_mod'] and data['last_mod'] != "None":
                data['last_mods'] = [data['last_mod']]
            else:
                data['last_mods'] = []
            # Remove the old field to avoid confusion
            del data['last_mod']
        
        return data
    
    def save(self, path: str = None):
        """Save configuration to JSON file."""
        if path is None:
            script_dir = os.path.dirname(os.path.abspath(__file__))
            path = os.path.join(script_dir, "war_analyzer_config.json")
        
        try:
            config_dict = {
                'recent_files': self.recent_files,
                'window_position': self.window_position,
                'default_mods': self.default_mods,
                'auto_load_last': self.auto_load_last,
                'last_mods': self.last_mods
            }
            
            with open(path, 'w') as f:
                json.dump(config_dict, f, indent=2)
                
        except Exception as e:
            print(f"Error saving config: {e}")
            # Silently fail if saving config fails
            pass
    
    def get_active_mods_display(self) -> str:
        """Get a display string for the active mods."""
        if not self.default_mods:
            return "None"
        return ", ".join(self.default_mods)
    
    def has_mods(self) -> bool:
        """Check if any mods are configured."""
        return len(self.default_mods) > 0
    
    def add_mod(self, mod_name: str):
        """Add a mod to the configuration if it doesn't already exist."""
        if mod_name and mod_name not in self.default_mods:
            self.default_mods.append(mod_name)
    
    def remove_mod(self, mod_name: str):
        """Remove a mod from the configuration."""
        if mod_name in self.default_mods:
            self.default_mods.remove(mod_name)
    
    def set_mods(self, mod_list: List[str]):
        """Set the mod list, replacing any existing mods."""
        self.default_mods = [mod for mod in mod_list if mod]  # Filter out empty strings
    
    def clear_mods(self):
        """Clear all mods from the configuration."""
        self.default_mods.clear()
        self.last_mods.clear()

@dataclass
class AppState:
    vic2_path: Optional[str] = None
    mod_names: List[str] = field(default_factory=list)  # Changed from mod_name to mod_names
    save_file_path: str = "No save file selected"
    text_renderer: Optional['TextRenderer'] = None
    
    # Data caching
    _wars_data: Optional[List['War']] = None
    _save_file_text: Optional[str] = None
    _parsed_great_nations: Optional[Set[str]] = None
    _parsed_country_governments: Optional[Dict[str, str]] = None
    _parsed_country_cultures: Optional[Dict[str, str]] = None
    _localization_cache: Optional[Dict[str, str]] = None
    _government_cache_loaded: bool = False
    _culture_mappings_cache: Optional[Dict[str, str]] = None
    _cb_types_cache: Optional[Dict[str, int]] = field(default_factory=dict)
    _unit_types_cache: Optional[Dict[str, str]] = None
    
    # Image caches
    image_cache: 'ImageCache' = None
    war_image_cache: 'ImageCache' = None
    
    # Runtime state
    filtered_wars: List[War] = field(default_factory=list)
    selected_war_index: Optional[int] = None
    selected_countries: Set[str] = field(default_factory=set)
    
    # Game data
    gov_to_flagtype: Dict[str, str] = field(default_factory=dict)
    gov_index_map: Dict[str, str] = field(default_factory=dict)
    
    # Configuration
    config: AppConfig = field(default_factory=AppConfig)
    
    def __post_init__(self):
        # Initialize image caches if not provided
        if self.image_cache is None:
            self.image_cache = ImageCache(max_size=1000)
        if self.war_image_cache is None:
            self.war_image_cache = ImageCache(max_size=800)
        
        # Set mods from config if not explicitly provided
        if not self.mod_names and self.config and self.config.default_mods:
            self.mod_names = self.config.default_mods.copy()
    
    # Add property for save_file_text
    @property
    def save_file_text(self) -> str:
        return self._save_file_text or ""
    
    @save_file_text.setter
    def save_file_text(self, value: str):
        self._save_file_text = value
    
    @property
    def localization_names(self) -> Dict[str, str]:
        if self._localization_cache is None:
            self._localization_cache = LocalizationParser.parse_localization_files(self)
        return self._localization_cache
    
    @localization_names.setter
    def localization_names(self, value: Dict[str, str]):
        self._localization_cache = value
    
    @property
    def wars_data(self) -> List[War]:
        return self._wars_data or []
    
    @wars_data.setter
    def wars_data(self, value: List[War]):
        self._wars_data = value
    
    @property
    def great_nations(self) -> Set[str]:
        return self._parsed_great_nations or set()
    
    @great_nations.setter
    def great_nations(self, value: Set[str]):
        self._parsed_great_nations = value
    
    @property
    def country_governments(self) -> Dict[str, str]:
        return self._parsed_country_governments or {}
    
    @country_governments.setter
    def country_governments(self, value: Dict[str, str]):
        self._parsed_country_governments = value
    
    @property
    def _country_cultures(self) -> Dict[str, str]:
        return self._parsed_country_cultures or {}
    
    @_country_cultures.setter 
    def _country_cultures(self, value: Dict[str, str]):
        self._parsed_country_cultures = value
    
    def get_modded_path(self, relative_path: str) -> str:
        """Get the path to a file or directory, checking mod folders in order then base game."""
        # Check each mod in the specified order
        for mod_name in self.mod_names:
            if mod_name and mod_name != "None" and self.vic2_path:
                mod_path = Path(self.vic2_path) / "mod" / mod_name / relative_path
                if mod_path.exists():
                    return str(mod_path)
        
        # Fallback to base game
        if self.vic2_path:
            base_path = Path(self.vic2_path) / relative_path
            if base_path.exists():
                return str(base_path)
        
        return relative_path
    
    def set_mods_from_config(self):
        """Set mod names from the configuration."""
        if self.config:
            self.mod_names = self.config.default_mods.copy()
    
    def get_active_mods_display(self) -> str:
        """Get a display string for the active mods."""
        if not self.mod_names:
            return "None"
        return ", ".join(self.mod_names)
    
    def has_mods(self) -> bool:
        """Check if any mods are active."""
        return len(self.mod_names) > 0
    
    @property
    def mod_name(self) -> str:
        """Backward compatibility property - returns first mod or 'None'"""
        if self.mod_names:
            return self.mod_names[0]
        return "None"
    
    @mod_name.setter
    def mod_name(self, value: str):
        """Backward compatibility setter - converts single mod to list"""
        if value == "None" or not value:
            self.mod_names = []
        else:
            self.mod_names = [value]
    
class ModManager:
    """Manages multiple mod loading order and file resolution."""
    
    @staticmethod
    def get_available_mods(vic2_path: str) -> List[str]:
        """Get list of available mods by reading all folders in the mod directory."""
        if not vic2_path:
            return []
            
        mods_path = Path(vic2_path) / "mod"
        if not mods_path.exists():
            return []
        
        available_mods = []
        
        # Simply get all directories in the mod folder
        for item in mods_path.iterdir():
            if item.is_dir():
                # Skip system folders
                if item.name.lower() in ['.git', '.svn', '__pycache__', 'backup', 'temp']:
                    continue
                available_mods.append(item.name)
        
        return sorted(available_mods)
    
    @staticmethod
    def get_available_mods_with_info(vic2_path: str) -> List[Tuple[str, str]]:
        """Get mods - simplified to just return folder names."""
        if not vic2_path:
            return []
            
        mods_path = Path(vic2_path) / "mod"
        if not mods_path.exists():
            return []
        
        available_mods = []
        
        for item in mods_path.iterdir():
            if item.is_dir():
                mod_name = item.name
                
                # Skip system folders
                if mod_name.lower() in ['.git', '.svn', '__pycache__', 'backup', 'temp']:
                    continue
                
                # Just return the folder name with empty info string
                available_mods.append((mod_name, ""))
        
        return sorted(available_mods, key=lambda x: x[0])
    
    @staticmethod
    def validate_mod_order(mod_names: List[str], vic2_path: str) -> List[str]:
        """Validate that all mods in the list exist and remove invalid ones."""
        if not vic2_path:
            return []
            
        available_mods = set(ModManager.get_available_mods(vic2_path))
        valid_mods = []
        
        for mod_name in mod_names:
            if mod_name in available_mods:
                valid_mods.append(mod_name)
            else:
                print(f"Warning: Mod '{mod_name}' not found in mods directory, skipping")
        
        return valid_mods

@dataclass
class FontChar:
    id: int
    x: int
    y: int
    width: int
    height: int
    xoffset: int
    yoffset: int
    xadvance: int
    page: int

class CleanBMFont:
    def __init__(self):
        self.reset_font()
        
    def reset_font(self):
        """Reset all font data to initial state"""
        self.chars: Dict[int, FontChar] = {}
        self.common = {}
        self.pages: List[Image.Image] = []
        self.kernings: Dict[Tuple[int, int], int] = {}
        self._color = (255, 255, 255)
        
    def load_font(self, fnt_path: str) -> bool:
        """Load font from .fnt file with proper baseline alignment"""
        try:
            # RESET before loading new font
            self.reset_font()
            
            with open(fnt_path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
            
            # Parse the font file
            self._parse_font_file_complete(content, fnt_path)
            
            return True
            
        except Exception as e:
            import traceback
            traceback.print_exc()
            return False
    
    def _parse_font_file_complete(self, content: str, fnt_path: str):
        """Parse the complete font file"""
        lines = content.split('\n')
        base_dir = os.path.dirname(fnt_path)
        
        # Find page definitions
        page_files = {}
        for line in lines:
            line = line.strip()
            if line.startswith('page'):
                parts = line.split()
                page_id = None
                file_name = None
                
                for part in parts[1:]:
                    if part.startswith('id='):
                        page_id = int(part.split('=')[1])
                    elif part.startswith('file='):
                        file_name = part.split('=')[1]
                        if file_name.startswith('"') and file_name.endswith('"'):
                            file_name = file_name[1:-1]
                
                if page_id is not None and file_name is not None:
                    page_files[page_id] = file_name
        
        # Guess texture if needed
        if not page_files:
            fnt_name = os.path.splitext(os.path.basename(fnt_path))[0]
            guessed_files = [
                f"{fnt_name}_0.tga", f"{fnt_name}_0.png", f"{fnt_name}.tga", f"{fnt_name}.png",
            ]
            
            for guessed_file in guessed_files:
                test_path = os.path.join(base_dir, guessed_file)
                if os.path.exists(test_path):
                    page_files[0] = guessed_file
                    break
        
        # Load textures
        for page_id, file_name in page_files.items():
            texture_path = os.path.join(base_dir, file_name)
            if os.path.exists(texture_path):
                self.pages.append(Image.open(texture_path).convert('RGBA'))
            else:
                scaleW = int(self.common.get('scaleW', 256))
                scaleH = int(self.common.get('scaleH', 256))
                placeholder = Image.new('RGBA', (scaleW, scaleH), (50, 50, 50, 255))
                self.pages.append(placeholder)
        
        # Parse other data
        for line in lines:
            line = line.strip()
            if not line:
                continue
                
            try:
                if line.startswith('info'):
                    self._parse_key_value_line(line, 'info')
                elif line.startswith('common'):
                    self._parse_key_value_line(line, 'common')
                elif line.startswith('char'):
                    self._parse_char_line(line)
                elif line.startswith('kerning'):
                    self._parse_kerning_line(line)
            except Exception as e:
                continue
    
    def _parse_key_value_line(self, line: str, line_type: str):
        """Parse info and common lines"""
        pairs = self._extract_key_value_pairs(line)
        if line_type == 'common':
            self.common = pairs
    
    def _parse_char_line(self, line: str):
        """Parse character line"""
        pairs = self._extract_key_value_pairs(line)
        
        if 'id' not in pairs:
            return
            
        try:
            char_id = int(pairs['id'])
            self.chars[char_id] = FontChar(
                id=char_id,
                x=int(pairs.get('x', 0)),
                y=int(pairs.get('y', 0)),
                width=int(pairs.get('width', 0)),
                height=int(pairs.get('height', 0)),
                xoffset=int(pairs.get('xoffset', 0)),
                yoffset=int(pairs.get('yoffset', 0)),
                xadvance=int(pairs.get('xadvance', 0)),
                page=int(pairs.get('page', 0))
            )
        except:
            pass
    
    def _parse_kerning_line(self, line: str):
        """Parse kerning line"""
        pairs = self._extract_key_value_pairs(line)
        
        try:
            first = int(pairs.get('first', 0))
            second = int(pairs.get('second', 0))
            amount = int(pairs.get('amount', 0))
            
            if first and second:
                self.kernings[(first, second)] = amount
        except:
            pass
    
    def _extract_key_value_pairs(self, line: str) -> Dict[str, str]:
        """Extract key=value pairs"""
        pairs = {}
        parts = line.split()
        
        for part in parts[1:]:
            if '=' in part:
                key, value = part.split('=', 1)
                if value.startswith('"') and value.endswith('"'):
                    value = value[1:-1]
                pairs[key] = value
        
        return pairs
    
    def get_kerning(self, first: int, second: int) -> int:
        """Get kerning between two characters"""
        return self.kernings.get((first, second), 0)
    
    def set_color(self, color: Tuple[int, int, int]):
        """Set text color"""
        self._color = color
    
    def render_text(self, text: str, size: int = 18, color: str = "black", 
               bold: bool = False, vertical_offset: int = 0) -> Image.Image:
        """Render text with proper baseline alignment - CLEAN VERSION"""
        if color is None:
            color = self._color
        
        if not self.common or not self.chars:
            return self._create_error_image("No font data")
        
        # Get font metrics
        line_height = int(self.common.get('lineHeight', 18))
        
        # Calculate size with kerning
        width, height = self._calculate_text_size_with_kerning(text, line_height)
        
        # Create clean image
        image = Image.new('RGBA', (width, height), (0, 0, 0, 0))
        
        x, y = 0, 0
        prev_char = None
        
        for char in text:
            char_code = ord(char)
            
            if char_code not in self.chars:
                if prev_char is not None:
                    x += 8
                prev_char = char_code
                continue
            
            char_info = self.chars[char_code]
            
            # Apply kerning
            if prev_char is not None:
                kerning = self.get_kerning(prev_char, char_code)
                x += kerning
            
            # Baseline alignment - clean version
            dest_x = x + char_info.xoffset
            dest_y = y + char_info.yoffset + vertical_offset
            
            # Draw character from texture
            if (self.pages and char_info.page < len(self.pages) and 
                char_info.width > 0 and char_info.height > 0):
                
                texture = self.pages[char_info.page]
                if (char_info.x + char_info.width <= texture.width and 
                    char_info.y + char_info.height <= texture.height):
                    
                    char_region = texture.crop((
                        char_info.x, char_info.y,
                        char_info.x + char_info.width,
                        char_info.y + char_info.height
                    ))
                    
                    # Apply color
                    if char_region.mode != 'RGBA':
                        char_region = char_region.convert('RGBA')

                    # Convert color name to RGB tuple
                    if color == "black":
                        color_rgb = (0, 0, 0)
                    else:  # Default to white for anything else
                        color_rgb = (255, 255, 255)

                    colored_char = Image.new('RGBA', char_region.size, (*color_rgb, 0))
                    alpha = char_region.split()[3]
                    colored_char.putalpha(alpha)

                    # Clean paste
                    image.paste(colored_char, (dest_x, dest_y), colored_char)
            
            # Advance to next character
            x += char_info.xadvance
            prev_char = char_code
        
        return image
    
    def _calculate_text_size_with_kerning(self, text: str, line_height: int) -> Tuple[int, int]:
        """Calculate text size including kerning"""
        width = 0
        prev_char = None
        
        for char in text:
            char_code = ord(char)
            
            if char_code in self.chars:
                if prev_char is not None:
                    width += self.get_kerning(prev_char, char_code)
                
                width += self.chars[char_code].xadvance
                prev_char = char_code
            else:
                width += 8
                prev_char = None
        
        return (max(100, width), max(50, line_height))
    
    def _create_error_image(self, message: str) -> Image.Image:
        """Create an error image"""
        img = Image.new('RGB', (400, 100), (255, 200, 200))
        draw = ImageDraw.Draw(img)
        draw.text((10, 10), f"ERROR: {message}", fill=(255, 0, 0))
        return img

class EasyFont:
    """Simple API for using BMFonts in other programs"""
    def __init__(self, fnt_path: str = None):
        self.font = CleanBMFont()
        if fnt_path:
            self.load_font(fnt_path)
    
    def load_font(self, fnt_path: str) -> bool:
        """Load a font from .fnt file - automatically resets previous font"""
        return self.font.load_font(fnt_path)
    
    def set_color(self, r: int, g: int, b: int):
        """Set text color using RGB values (0-255)"""
        self.font.set_color((r, g, b))
    
    def render(self, text: str, color = None) -> Image.Image:
        """Render text to PIL Image - support color names by converting to RGB"""
        if color is None:
            color = "white"
        
        # Convert color names to RGB tuples
        color_map = {
            "white": (255, 255, 255),
            "black": (0, 0, 0),
            "red": (255, 0, 0),
            "green": (0, 255, 0), 
            "blue": (0, 0, 255),
            "yellow": (255, 255, 0),
        }
        
        # If color is a string, convert to RGB tuple
        if isinstance(color, str):
            color_rgb = color_map.get(color.lower(), (255, 255, 255))
        else:
            # Assume it's already an RGB tuple
            color_rgb = color
        
        # Set the color first
        self.font.set_color(color_rgb)
        
        # Render the text - don't pass color again if render_text doesn't use it
        text_image = self.font.render_text(text)
        
        # Always convert the image to the desired color
        data = text_image.getdata()
        new_data = []
        for item in data:
            # Use the alpha channel from the original, but replace RGB with our color
            if item[3] > 0:  # If pixel is not fully transparent
                new_data.append((color_rgb[0], color_rgb[1], color_rgb[2], item[3]))
            else:
                new_data.append(item)
        text_image.putdata(new_data)
        
        return text_image
    
    def save_image(self, text: str, filename: str, color: Tuple[int, int, int] = None):
        """Render text and save to file"""
        image = self.render(text, color)
        image.save(filename)
    
class CustomCheckbox:
    """Custom checkbox using game graphics."""
    
    def __init__(self, parent, state, config_field, text, x, y, callback=None):
        self.parent = parent
        self.state = state
        self.config_field = config_field
        self.text = text
        self.x = x
        self.y = y
        self.callback = callback
        self.images = {}
        self._load_images()
        self._create_widgets()
    
    def _load_images(self):
        """Load checkbox images."""
        checkbox_path = self.state.get_modded_path(os.path.join("gfx", "interface", "checkbox_default.dds"))
        checkbox_img = SafeLoader.safe_load_image(checkbox_path)
        
        if checkbox_img:
            # Crop 20x20 from left (checked) and right (unchecked)
            self.images['checked'] = checkbox_img.crop((20, 0, 40, 20))
            self.images['unchecked'] = checkbox_img.crop((0, 0, 20, 20))
        else:
            # Fallback: create simple colored boxes
            self.images['checked'] = Image.new("RGBA", (20, 20), (0, 255, 0, 255))  # Green
            self.images['unchecked'] = Image.new("RGBA", (20, 20), (255, 0, 0, 255))  # Red
        
        # Convert to PhotoImage
        self.images['checked_photo'] = ImageTk.PhotoImage(self.images['checked'])
        self.images['unchecked_photo'] = ImageTk.PhotoImage(self.images['unchecked'])
    
    def _create_widgets(self):
        """Create the checkbox widgets."""
        # Get current value from config
        self.var = tk.BooleanVar(value=getattr(self.state.config, self.config_field))
        
        # Create container frame
        self.frame = tk.Frame(self.parent, bg=BG_COLOR)
        
        # Create checkbox image label
        self.checkbox_label = tk.Label(self.frame, 
                                     image=self.images['checked_photo'] if self.var.get() else self.images['unchecked_photo'],
                                     cursor="hand2", 
                                     bg=BG_COLOR,
                                     borderwidth=0)
        self.checkbox_label.pack(side=tk.LEFT)
        
        # Create text label
        self.text_label = tk.Label(self.frame, 
                                 text=self.text, 
                                 bg=BG_COLOR, 
                                 font=("Arial", 10),
                                 cursor="hand2")
        self.text_label.pack(side=tk.LEFT, padx=(5, 0))
        
        # Bind click events
        self.checkbox_label.bind("<Button-1>", self._toggle)
        self.text_label.bind("<Button-1>", self._toggle)
        
        # Position the frame
        self.canvas_id = self.parent.create_window(self.x, self.y, anchor=tk.NW, window=self.frame)
    
    def _toggle(self, event=None):
        """Toggle checkbox state."""
        new_value = not self.var.get()
        self.var.set(new_value)
        
        # Update image
        if new_value:
            self.checkbox_label.config(image=self.images['checked_photo'])
        else:
            self.checkbox_label.config(image=self.images['unchecked_photo'])
        
        # Update config
        setattr(self.state.config, self.config_field, new_value)
        self.state.config.save()
        
        # Call callback if provided
        if self.callback:
            self.callback(new_value)
    
    def get_value(self):
        """Get current checkbox value."""
        return self.var.get()
    
    def set_value(self, value):
        """Set checkbox value."""
        self.var.set(value)
        if value:
            self.checkbox_label.config(image=self.images['checked_photo'])
        else:
            self.checkbox_label.config(image=self.images['unchecked_photo'])

# ============================================================================
# DYNAMIC LEADER SYSTEM
# ============================================================================

class CultureLeaderMapper:
    @staticmethod
    def load_culture_group_mappings(state: AppState) -> Dict[str, str]:
        if state._culture_mappings_cache is not None:
            return state._culture_mappings_cache
            
        culture_to_leader = {}
        cultures_path = state.get_modded_path(os.path.join("common", "cultures.txt"))
        
        if not os.path.exists(cultures_path):
            return culture_to_leader
        
        try:
            with open(cultures_path, 'r', encoding='latin-1') as f:
                content = f.read()
            
            # Remove comments to simplify parsing
            content = re.sub(r'#.*', '', content)
            
            # Parse culture groups and their leader types
            group_pattern = re.compile(r'([a-zA-Z_]+)\s*=\s*\{', re.DOTALL)
            pos = 0
            
            while True:
                group_match = group_pattern.search(content, pos)
                if not group_match:
                    break
                    
                group_name = group_match.group(1)
                group_block_start = group_match.end() - 1
                
                # Extract the entire culture group block
                group_block = SavefileParser._extract_block(content, group_block_start)
                if not group_block:
                    pos = group_match.end()
                    continue
                
                # Extract leader type for this culture group
                leader_match = re.search(r'leader\s*=\s*([a-zA-Z_]+)', group_block)
                if not leader_match:
                    pos = group_block_start + len(group_block)
                    continue
                    
                leader_type = leader_match.group(1)
                
                # Find culture definitions within this group block
                culture_pattern = re.compile(r'([a-zA-Z_]+)\s*=\s*\{', re.DOTALL)
                culture_pos = 0
                
                while True:
                    culture_match = culture_pattern.search(group_block, culture_pos)
                    if not culture_match:
                        break
                    
                    culture_name = culture_match.group(1)
                    
                    # Skip known group-level properties
                    if culture_name in ['leader', 'unit', 'graphical_culture', 'color', 'is_overseas', 'union']:
                        culture_pos = culture_match.end()
                        continue
                    
                    # Skip sub-properties
                    if culture_name in ['first_names', 'last_names', 'radicalism', 'primary']:
                        culture_pos = culture_match.end()
                        continue
                    
                    # Extract the culture block to verify it's a real culture
                    culture_block_start = culture_match.end() - 1
                    culture_block = SavefileParser._extract_block(group_block, culture_block_start)
                    
                    if culture_block:
                        # Check if it has culture properties
                        if re.search(r'(first_names|last_names|color|radicalism|primary)\s*=', culture_block):
                            culture_to_leader[culture_name] = leader_type
                    
                    culture_pos = culture_match.end()
                
                pos = group_block_start + len(group_block)
        
        except Exception:
            # Silently fail and return empty mapping
            pass
        state._culture_mappings_cache = culture_to_leader
        return culture_to_leader
    
    @staticmethod
    def detect_cultures_and_governments(save_text: str, gov_index_map: Dict[str, str]) -> Tuple[Dict[str, str], Dict[str, str]]:
        """Detect BOTH cultures and governments for all countries using the same reliable method."""
        tag_to_culture = {}
        tag_to_gov = {}
        
        # Use the EXACT same logic as SavefileParser.detect_governments but also get cultures
        for match in re.finditer(r'country\s*=\s*\{', save_text):
            block = SavefileParser._extract_block(save_text, match.end() - 1)
            
            tag_match = re.search(r'tag\s*=\s*"([A-Z0-9-]{3})"', block)
            if not tag_match:
                continue
            
            tag = tag_match.group(1)
            
            # Get culture
            culture_match = re.search(r'primary_culture\s*=\s*"([^"]+)"', block)
            if culture_match:
                culture = culture_match.group(1).strip()
                tag_to_culture[tag] = culture
            
            # Get government (same as original)
            gov_match = re.search(r'government\s*=\s*("?([A-Za-z_]+)"?|\d+)', block)
            if gov_match:
                val = gov_match.group(1).strip('"')
                gov = gov_index_map.get(val, val) if val.isdigit() else val
                tag_to_gov[tag] = gov
        
        for match in re.finditer(r'([A-Z0-9-]{3})\s*=\s*\{', save_text):
            tag = match.group(1)
            block = SavefileParser._extract_block(save_text, match.end() - 1)
            
            # Get culture
            culture_match = re.search(r'primary_culture\s*=\s*"([^"]+)"', block)
            if culture_match:
                culture = culture_match.group(1).strip()
                tag_to_culture[tag] = culture
            
            # Get government (same as original)
            gov_match = re.search(r'government\s*=\s*("?([A-Za-z_]+)"?|\d+)', block)
            if gov_match:
                val = gov_match.group(1).strip('"')
                gov = gov_index_map.get(val, val) if val.isdigit() else val
                tag_to_gov[tag] = gov
        
        return tag_to_culture, tag_to_gov
    
    @staticmethod
    def get_leader_graphics(state: AppState, country_tag: str, is_naval: bool = False) -> Optional[str]:
        """Get appropriate leader graphics for a country."""
        # Load culture group mappings if not already loaded
        if not hasattr(state, '_culture_to_leader'):
            state._culture_to_leader = CultureLeaderMapper.load_culture_group_mappings(state)
        
        # Get country's specific culture
        if not hasattr(state, '_country_cultures'):
            leader_type = "european"
        else:
            specific_culture = state._country_cultures.get(country_tag)
            
            if not specific_culture:
                leader_type = "european"
            else:
                # Map specific culture to leader type using the loaded mappings
                leader_type = state._culture_to_leader.get(specific_culture, "european")
        
        # Determine leader file pattern
        if is_naval:
            leader_pattern = f"{leader_type}_admiral_*.dds"
        else:
            leader_pattern = f"{leader_type}_general_*.dds"
        
        # Find all matching leader files with mod support
        leaders_path = state.get_modded_path(os.path.join("gfx", "interface", "leaders"))
        
        if not os.path.exists(leaders_path):
            return None
        
        # Get all matching files
        import glob
        pattern = os.path.join(leaders_path, leader_pattern)
        matching_files = glob.glob(pattern)
        
        if not matching_files:
            # Fallback to default European
            if is_naval:
                fallback_pattern = "european_admiral_*.dds"
            else:
                fallback_pattern = "european_general_*.dds"
            
            fallback_pattern_path = os.path.join(leaders_path, fallback_pattern)
            matching_files = glob.glob(fallback_pattern_path)
            
            if not matching_files:
                return None
        
        # Pick a random leader file
        import random
        selected_leader = random.choice(matching_files)
        
        return selected_leader

# ============================================================================
# OPTIMIZED PARSER INTEGRATION
# ============================================================================

class EnhancedBattleParser:
    """Enhanced battle parser that extracts ALL unit types including modded ones."""

    @staticmethod
    def parse_battle_units_with_variants(side_block: str) -> Dict:
        """Parse ALL units including defensive/offensive variants."""
        units = {}
        
        # Look for any unit type pattern (word followed by = number)
        unit_pattern = re.compile(r'(\w+)\s*=\s*(\d+)')
        
        # List of fields to exclude (non-unit fields)
        exclude_fields = {'losses', 'country', 'leader'}
        
        for match in unit_pattern.finditer(side_block):
            unit_name = match.group(1)
            count = int(match.group(2))
            
            # Skip non-unit fields
            if unit_name in exclude_fields:
                continue
            
            # Include ALL units including variants
            units[unit_name] = count
        
        return units

class OptimizedSavefileParser:
    """Comprehensive war parser that extracts all war details."""
    @staticmethod
    def parse_great_nations(save_text: str) -> Set[str]:
        """Parse great nations from save file using save file order only."""
        great_nations = set()
        
        # Parse country order directly from save file
        save_countries = []
        
        # Look for the pattern: TAG=\n{\n    tax_base= (or similar whitespace)
        pattern = re.compile(r'^\s*([A-Z]{3})\s*=\s*\n\s*\{[^}]*?tax_base\s*=', re.MULTILINE | re.DOTALL)
        matches = pattern.findall(save_text)
        
        # Remove duplicates while preserving order
        seen = set()
        for tag in matches:
            if tag not in seen:
                seen.add(tag)
                save_countries.append(tag)
        
        # If no countries found with tax_base pattern, try compact format as fallback
        if not save_countries:
            pattern_compact = re.compile(r'^\s*([A-Z]{3})\s*=\s*\{[^{}]{0,200}?tax_base\s*=', re.MULTILINE | re.DOTALL)
            compact_matches = pattern_compact.findall(save_text)
            
            seen_compact = set()
            for tag in compact_matches:
                if tag not in seen_compact:
                    seen_compact.add(tag)
                    save_countries.append(tag)
        
        # Parse great nations using save file order
        match = re.search(r'great_nations\s*=\s*\{([^}]+)\}', save_text)
        if not match:
            return great_nations
        
        great_nations_block = match.group(1)
        
        # Extract numbers from the block
        numbers = re.findall(r'\b(\d+)\b', great_nations_block)
        
        # Convert numbers to country tags using SAVE FILE ORDER
        for num in numbers:
            num_int = int(num)
            if num_int - 1 < len(save_countries):
                save_file_tag = save_countries[num_int - 1]
                great_nations.add(save_file_tag)
        
        return great_nations

    @staticmethod
    def parse_wars(path: str) -> Tuple[List[War], str]:
        """Parse wars from save file using comprehensive extraction."""
        try:
            with open(path, 'r', encoding='latin-1') as f:
                text = f.read()
        except Exception:
            return [], ""
        
        wars = []
        
        # Look for both active and previous wars
        war_indicators = ['active_war=', 'previous_war=']
        
        for indicator in war_indicators:
            pos = 0
            while True:
                war_start = text.find(indicator, pos)
                if war_start == -1:
                    break
                
                # Parse the war block and pass the indicator type
                block, new_pos = OptimizedSavefileParser._extract_block(text, war_start)
                if block:
                    # Pass the indicator type to the parser
                    war = OptimizedSavefileParser._parse_war_block(block, indicator)
                    if war:
                        wars.append(war)
                pos = new_pos if new_pos > pos else pos + 1
        
        return wars, text
    
    @staticmethod
    def _extract_block(text: str, start: int) -> Tuple[Optional[str], int]:
        """Extract a brace-delimited block."""
        brace_count = 0
        pos = start
        block_start = None
        
        # Find the opening brace
        while pos < len(text) and text[pos] != '{':
            pos += 1
        
        if pos >= len(text):
            return None, pos
        
        block_start = pos
        brace_count = 1
        pos += 1
        
        # Find matching closing brace
        while pos < len(text) and brace_count > 0:
            if text[pos] == '{':
                brace_count += 1
            elif text[pos] == '}':
                brace_count -= 1
            pos += 1
        
        if brace_count == 0:
            return text[block_start:pos], pos
        return None, pos
    
    @staticmethod
    def _parse_war_block(block: str, indicator: str = "") -> Optional[War]:
        """Parse a single war block into a War object."""
        name_match = re.search(r'name="([^"]+)"', block)
        name = name_match.group(1) if name_match else "(no name)"
        
        # Determine if this is an active or previous war based on the indicator
        is_active = indicator == 'active_war='
        
        attackers = set()
        defenders = set()

        oa = re.search(r'original_attacker="([^"]+)"', block)
        od = re.search(r'original_defender="([^"]+)"', block)

        if oa:
            attackers.add(oa.group(1))
        if od:
            defenders.add(od.group(1))

        for match in re.finditer(r'add_attacker="([^"]+)"', block):
            attackers.add(match.group(1))
        for match in re.finditer(r'add_defender="([^"]+)"', block):
            defenders.add(match.group(1))
        
        # Now parse war participation with dates SEPARATELY for date calculation only
        participation_events = []
        
        # Find all participation changes with dates
        for match in re.finditer(r'(add_attacker|add_defender|rem_attacker|rem_defender)\s*=\s*"([^"]+)"\s*(\d+\.\d+\.\d+)?', block):
            action = match.group(1)  # add_attacker, add_defender, rem_attacker, rem_defender
            country = match.group(2)
            date = match.group(3) if match.group(3) else None
            
            participation_events.append({
                'action': action,
                'country': country,
                'date': date
            })
        
        # Calculate war dates based on participation events
        start_date = None
        end_date = None
        
        # Parse war participation with dates - Victoria 2 format is different!
        participation_events = []

        # Look for date blocks containing participation events
        # Format: 1836.7.31={ add_attacker="NOV" }
        for match in re.finditer(r'(\d+\.\d+\.\d+)\s*=\s*\{([^}]+)\}', block):
            date = match.group(1)
            inner_block = match.group(2)
            
            # Check for participation events inside this date block
            for action_match in re.finditer(r'(add_attacker|add_defender|rem_attacker|rem_defender)\s*=\s*"([^"]+)"', inner_block):
                action = action_match.group(1)
                country = action_match.group(2)
                
                participation_events.append({
                    'action': action,
                    'country': country,
                    'date': date
                })

        start_date = None
        end_date = None

        def date_to_sortable(date_str):
            """Convert YYYY.M.D to YYYYMMDD for proper sorting"""
            if not date_str:
                return "00000000"
            try:
                parts = date_str.split('.')
                year = parts[0].zfill(4)
                month = parts[1].zfill(2) if len(parts) > 1 else "01"
                day = parts[2].zfill(2) if len(parts) > 2 else "01"
                return year + month + day
            except:
                return "00000000"

        if participation_events:
            # Sort by proper date format, not string comparison
            participation_events.sort(key=lambda x: date_to_sortable(x['date']))
            start_date = participation_events[0]['date']
            end_date = participation_events[-1]['date']

        # If no dates found from events, try direct fields as fallback
        if not start_date:
            start_match = re.search(r'start_date\s*=\s*"([^"]+)"', block)
            if start_match:
                start_date = start_match.group(1)

        if not end_date and 'previous_war=' in block:
            end_match = re.search(r'end_date\s*=\s*"([^"]+)"', block)
            if end_match:
                end_date = end_match.group(1)
        
        # ENHANCED: Parse war goals with proper multi-line support
        goals = []

        # Parse war goals using the _extract_block method to handle multi-line blocks
        for match in re.finditer(r'war_?goal\s*=\s*\{', block):
            goal_block = OptimizedSavefileParser._extract_block(block, match.end() - 1)
            if goal_block:
                goal_content = goal_block[0]
                
                # Extract fields from the multi-line block
                cb_match = re.search(r'casus_belli\s*=\s*"([^"]+)"', goal_content)
                actor_match = re.search(r'actor\s*=\s*"([^"]+)"', goal_content)
                receiver_match = re.search(r'receiver\s*=\s*"([^"]+)"', goal_content)
                spid_match = re.search(r'state_province_id\s*=\s*(\d+)', goal_content)
                
                if cb_match:
                    actor = actor_match.group(1) if actor_match else None
                    receiver = receiver_match.group(1) if receiver_match else None
                    
                    # If actor/receiver are missing, use war leaders as fallback
                    if not actor:
                        # Get war leaders from the main war participants
                        attackers = set()
                        defenders = set()
                        
                        oa = re.search(r'original_attacker="([^"]+)"', block)
                        if oa and oa.group(1) != "---":
                            attackers.add(oa.group(1))
                        
                        od = re.search(r'original_defender="([^"]+)"', block)  
                        if od and od.group(1) != "---":
                            defenders.add(od.group(1))
                        
                        for add_match in re.finditer(r'add_attacker="([^"]+)"', block):
                            if add_match.group(1) != "---":
                                attackers.add(add_match.group(1))
                        for add_match in re.finditer(r'add_defender="([^"]+)"', block):
                            if add_match.group(1) != "---":
                                defenders.add(add_match.group(1))
                        
                        actor = sorted(attackers)[0] if attackers else "UNK"
                        receiver = sorted(defenders)[0] if defenders else "UNK"
                    
                    cb = cb_match.group(1)
                    state_id = spid_match.group(1) if spid_match else None
                    
                    # Create war goal
                    if state_id:
                        goal_desc = f"{actor} {cb} (State {state_id}) against {receiver}"
                    else:
                        goal_desc = f"{actor} {cb} against {receiver}"
                    
                    war_goal = WarGoal(
                        description=goal_desc,
                        casus_belli=cb,
                        actor=actor,
                        receiver=receiver,
                        state_province_id=state_id
                    )
                    goals.append(war_goal)

        # Fallback for any loose casus_belli
        if not goals:
            cb_matches = re.findall(r'casus_belli\s*=\s*"([^"]+)"', block)
            for cb in cb_matches:
                if cb and cb != "none" and cb != "None":
                    war_goal = WarGoal(
                        description=f"{actor} {cb} against {receiver}",
                        casus_belli=cb,
                        actor=actor,
                        receiver=receiver,
                        state_province_id=None
                    )
                    goals.append(war_goal)
                
        battles = OptimizedSavefileParser._parse_battles(block)
        
        return War(
            name=name,
            attackers=sorted([t for t in attackers if t != "---"]),
            defenders=sorted([t for t in defenders if t != "---"]),
            goals=goals,
            battles=battles,
            original_attacker=oa.group(1) if oa else None,
            original_defender=od.group(1) if od else None,
            start_date=start_date,
            end_date=end_date,
            is_active=is_active
        )
    
    @staticmethod
    def _parse_battles(block: str) -> List[Battle]:
        """Parse all battles from a war block."""
        battles = []
        
        for match in re.finditer(r'battle\s*=\s*\{', block):
            battle_block = OptimizedSavefileParser._extract_block(block, match.end() - 1)
            if battle_block:
                battle = OptimizedSavefileParser._parse_battle_block(battle_block[0])
                if battle:
                    battles.append(battle)
        
        return battles
    
    @staticmethod
    def _parse_battle_block(block: str) -> Optional[Battle]:
        """Parse a single battle block with enhanced unit information."""
        name = re.search(r'name="([^"]+)"', block)
        name = name.group(1) if name else "(no name)"
        
        loc = re.search(r'location=(\d+)', block)
        location = loc.group(1) if loc else None
        
        # Enhanced side parsing - this will now extract units separately for each side
        attacker = OptimizedSavefileParser._extract_side_enhanced(block, 'attacker')
        defender = OptimizedSavefileParser._extract_side_enhanced(block, 'defender')
        
        result_match = re.search(r'result=("[^"]+"|yes|no|\w+)', block)
        result = None
        if result_match:
            rv = result_match.group(1).strip('"')
            if rv == 'yes':
                result = True
            elif rv == 'no':
                result = False
            else:
                result = rv
        
        return Battle(
            name=name,
            location=location,
            attacker=attacker,
            defender=defender,
            result=result
        )

    @staticmethod
    def _extract_side_enhanced(block: str, side: str) -> Dict:
        """Enhanced side extraction with leader and unit information."""
        match = re.search(rf'{side}\s*=\s*\{{(.*?)\}}', block, re.S)
        if not match:
            return {}
        
        side_text = match.group(1)
        info = {}
        
        # Basic information
        leader_match = re.search(r'leader="([^"]*)"', side_text)
        if leader_match:
            leader = leader_match.group(1).strip()
            info['leader'] = leader if leader else "No Commander"
        else:
            info['leader'] = "Unknown"
        
        country_match = re.search(r'country="([^"]+)"', side_text)
        if country_match:
            info['country'] = country_match.group(1)
        
        losses_match = re.search(r'losses=(\d+)', side_text)
        if losses_match:
            info['losses'] = int(losses_match.group(1))
        
        # Enhanced unit information - parse ONLY the units from this side's block
        units = EnhancedBattleParser.parse_battle_units_with_variants(side_text)
        info['units'] = units
        
        return info

# ============================================================================
# UPDATED BATTLE UNIT TYPE CLASSIFICATION
# ============================================================================

class UnitTypeClassifier:
    """Classifies units into land and naval categories and analyzes battles."""
    
    # Vanilla unit type mappings (fallback if unit files can't be parsed)
    VANILLA_UNIT_TYPES = {
        # Land - Infantry
        'infantry': 'infantry',
        'irregular': 'infantry',
        'guard': 'infantry',
        
        # Land - Cavalry
        'cavalry': 'cavalry',
        'cuirassier': 'cavalry',
        'dragoon': 'cavalry',
        'hussar': 'cavalry',
        
        # Land - Support (Artillery/Engineers) - will be grouped with Special
        'artillery': 'support_special',
        'engineer': 'support_special',
        
        # Land - Special (Tanks, etc.) - grouped with Support
        'tank': 'support_special',
        
        # Naval - Big Ships
        'manowar': 'big_ship',
        'ironclad': 'big_ship',
        'monitor': 'big_ship',
        'battleship': 'big_ship',
        'dreadnought': 'big_ship',
        
        # Naval - Light Ships
        'frigate': 'light_ship',
        'commerce_raider': 'light_ship',
        'cruiser': 'light_ship',
        
        # Naval - Transports
        'clipper_transport': 'transport',
        'transport': 'transport',
    }
    
    @staticmethod
    def load_unit_types(state: AppState) -> Dict[str, str]:
        if state._unit_types_cache is not None:
            return state._unit_types_cache
            
        unit_types = UnitTypeClassifier.VANILLA_UNIT_TYPES.copy()
        
        # Get the units folder path with mod support
        units_path = state.get_modded_path(os.path.join("units"))
        
        if not os.path.exists(units_path):
            # Try alternative paths - UPDATED: remove the old mod_name reference
            alternative_paths = [
                state.get_modded_path("units"),
                os.path.join(state.vic2_path, "units") if state.vic2_path else "",
            ]
            
            # Add paths for each mod individually (backward compatibility)
            if state.vic2_path and state.mod_names:
                for mod_name in state.mod_names:
                    mod_units_path = os.path.join(state.vic2_path, "mod", mod_name, "units")
                    alternative_paths.append(mod_units_path)
            
            for alt_path in alternative_paths:
                if alt_path and os.path.exists(alt_path):
                    units_path = alt_path
                    break
            else:
                return unit_types
        
        try:
            unit_files = [f for f in os.listdir(units_path) if f.endswith('.txt')]
            
            for filename in unit_files:
                unit_name = filename[:-4]  # Remove .txt extension
                file_path = os.path.join(units_path, filename)
                
                try:
                    with open(file_path, 'r', encoding='latin-1') as f:
                        content = f.read()
                    
                    # Parse unit_type from the file
                    unit_type_match = re.search(r'unit_type\s*=\s*"([^"]+)"', content)
                    if unit_type_match:
                        game_unit_type = unit_type_match.group(1)
                        category = UnitTypeClassifier._map_unit_type(game_unit_type)
                        unit_types[unit_name] = category
                    else:
                        # If no unit_type found, try to infer from name
                        category = UnitTypeClassifier._infer_unit_type(unit_name)
                        unit_types[unit_name] = category
                        
                except Exception:
                    # Add with inferred type as fallback
                    category = UnitTypeClassifier._infer_unit_type(unit_name)
                    unit_types[unit_name] = category
                    
        except Exception:
            # If scanning fails, return what we have (vanilla mappings)
            pass

        state._unit_types_cache = unit_types
        return unit_types
    
    @staticmethod
    def _map_unit_type(game_unit_type: str) -> str:
        """Map game unit types to our categories."""
        game_unit_type = game_unit_type.lower()
        
        # Land units
        if game_unit_type in ['infantry', 'irregular']:
            return 'infantry'
        elif game_unit_type in ['cavalry', 'cuirassier']:
            return 'cavalry'
        elif game_unit_type in ['artillery', 'engineer', 'tank']:
            return 'support_special'
        
        # Naval units
        elif game_unit_type in ['manowar', 'ironclad', 'battleship', 'dreadnought']:
            return 'big_ship'
        elif game_unit_type in ['frigate', 'commerce_raider', 'cruiser']:
            return 'light_ship'
        elif game_unit_type in ['transport', 'clipper_transport']:
            return 'transport'
        
        # Default to support_special for unknown land units
        else:
            return 'support_special'
    
    @staticmethod
    def _infer_unit_type(unit_name: str) -> str:
        """Infer unit type from unit name."""
        unit_name_lower = unit_name.lower()
        
        # Land - Infantry indicators
        if any(indicator in unit_name_lower for indicator in ['infantry', 'guard', 'irregular', 'militia', 'line']):
            return 'infantry'
        
        # Land - Cavalry indicators  
        elif any(indicator in unit_name_lower for indicator in ['cavalry', 'cuirassier', 'dragoon', 'hussar', 'lancer', 'horse']):
            return 'cavalry'
        
        # Land - Support/Special indicators (grouped together)
        elif any(indicator in unit_name_lower for indicator in ['artillery', 'engineer', 'support', 'howitzer', 'cannon', 'tank']):
            return 'support_special'
        
        # Naval - Big Ship indicators
        elif any(indicator in unit_name_lower for indicator in ['manowar', 'ironclad', 'battleship', 'dreadnought', 'monitor']):
            return 'big_ship'
        
        # Naval - Light Ship indicators
        elif any(indicator in unit_name_lower for indicator in ['frigate', 'cruiser', 'commerce_raider', 'raider']):
            return 'light_ship'
        
        # Naval - Transport indicators
        elif any(indicator in unit_name_lower for indicator in ['transport', 'clipper']):
            return 'transport'
        
        # Default to support_special for unknown land units
        else:
            return 'support_special'
    
    @staticmethod
    def is_naval_unit(unit_type: str) -> bool:
        """Check if a unit type is naval."""
        return unit_type in ['big_ship', 'light_ship', 'transport']
    
    @staticmethod
    def is_land_unit(unit_type: str) -> bool:
        """Check if a unit type is land."""
        return unit_type in ['infantry', 'cavalry', 'support_special']
    
    @staticmethod
    def analyze_battle_units(battle: Battle, unit_types: Dict[str, str]) -> Dict:
        """Analyze battle units by category for both sides."""
        attacker_units = battle.attacker.get('units', {})
        defender_units = battle.defender.get('units', {})
        
        result = {
            'attacker': UnitTypeClassifier._categorize_units(attacker_units, unit_types),
            'defender': UnitTypeClassifier._categorize_units(defender_units, unit_types)
        }
        
        return result
    
    @staticmethod
    def _categorize_units(units: Dict[str, int], unit_types: Dict[str, str]) -> Dict[str, int]:
        """Categorize units into land and naval types."""
        categorized = {
            # Land units - CAVALRY FIRST, then infantry, then support/special
            'cavalry': 0,
            'infantry': 0,
            'support_special': 0,
            # Naval units
            'big_ship': 0,
            'light_ship': 0,
            'transport': 0
        }
        
        for unit_name, count in units.items():
            # Get the category from our unit types mapping
            category = unit_types.get(unit_name, 'infantry')  # Default to infantry
            categorized[category] += count
        
        return categorized
    
    @staticmethod
    def get_battle_type(battle: Battle, unit_types: Dict[str, str]) -> str:
        """Determine if battle is land, naval, or mixed - OPTIMIZED VERSION"""
        attacker_units = battle.attacker.get('units', {})
        defender_units = battle.defender.get('units', {})
        
        has_land = False
        has_naval = False
        
        # Check attacker units with early exit
        for unit_name in attacker_units:
            unit_type = unit_types.get(unit_name, 'infantry')
            if UnitTypeClassifier.is_land_unit(unit_type):
                has_land = True
            elif UnitTypeClassifier.is_naval_unit(unit_type):
                has_naval = True
            if has_land and has_naval:
                break  # Early exit - we already know it's mixed
        
        # Check defender units only if we still need to determine battle type
        if not (has_land and has_naval):
            for unit_name in defender_units:
                unit_type = unit_types.get(unit_name, 'infantry')
                if UnitTypeClassifier.is_land_unit(unit_type):
                    has_land = True
                elif UnitTypeClassifier.is_naval_unit(unit_type):
                    has_naval = True
                if has_land and has_naval:
                    break  # Early exit
        
        if has_land and has_naval:
            return 'mixed'
        elif has_naval:
            return 'naval'
        else:
            return 'land'
    
    @staticmethod
    def is_naval_battle_for_panel(battle: Battle, unit_types: Dict[str, str]) -> bool:
        """Determine if a battle is naval for panel display purposes."""
        # Use the optimized battle type detection
        battle_type = UnitTypeClassifier.get_battle_type(battle, unit_types)
        return battle_type in ['naval', 'mixed']

# ============================================================================
# REST OF THE CODE (CB Types Parser, Image Utilities, etc.)
# ============================================================================

class CBTypesParser:
    """Parses casus belli types and their sprite indices."""
    
    @staticmethod
    def parse_cb_types(state: AppState) -> Dict[str, int]:
        """Parse cb_types.txt to get sprite indices for each CB with proper mod support."""
        cb_to_sprite = {}
        
        # Use state.get_modded_path to get the correct file (mod first, then base game)
        cb_types_path = state.get_modded_path(os.path.join("common", "cb_types.txt"))
        
        try:
            with open(cb_types_path, 'r', encoding='latin-1') as f:
                content = f.read()
        except (FileNotFoundError, Exception):
            return cb_to_sprite
        
        # Remove comments
        content = re.sub(r'#.*', '', content)
        
        # Parse each CB type
        cb_pattern = re.compile(r'([a-zA-Z_][a-zA-Z0-9_]*)\s*=\s*\{([^}]+)\}', re.DOTALL)
        
        matches = list(cb_pattern.finditer(content))
        
        for match in matches:
            cb_name = match.group(1)
            cb_block = match.group(2)
            
            # Extract sprite_index
            sprite_match = re.search(r'sprite_index\s*=\s*(\d+)', cb_block)
            if sprite_match:
                sprite_index = int(sprite_match.group(1))
                cb_to_sprite[cb_name] = sprite_index
        
        return cb_to_sprite

# ============================================================================
# IMAGE UTILITIES
# ============================================================================

class ImageCache:
    """Cache for images to improve performance."""
    def __init__(self, max_size: int = 1000):  # ADD THIS
        self.cache = {}
        self.max_size = max_size
        self.access_order = []
    
    def get(self, key: str) -> Optional[Image.Image]:
        if key in self.cache:
            self.access_order.remove(key)
            self.access_order.append(key)
            return self.cache[key]
        return None
    
    def set(self, key: str, image: Image.Image):
        if len(self.cache) >= self.max_size:
            lru_key = self.access_order.pop(0)
            del self.cache[lru_key]
        
        self.cache[key] = image
        self.access_order.append(key)
    
    def clear(self):
        self.cache.clear()
        self.access_order.clear()


class SafeLoader:
    """Safe loading utilities with fallbacks."""
    
    @staticmethod
    def safe_load_image(path: str, size: Optional[Tuple[int, int]] = None, 
                       default_color: Tuple[int, int, int, int] = (0, 0, 0, 0)) -> Image.Image:
        """Safely load image with fallback."""
        try:
            img = ImageLoader.load_with_alpha(path, size)
            if img:
                return img
        except Exception:
            pass
        
        if size:
            return Image.new("RGBA", size, default_color)
        return Image.new("RGBA", (FLAG_WIDTH, FLAG_HEIGHT), default_color)


class ImageLoader:
    """Handles image loading and processing."""
    
    @staticmethod
    def load_with_alpha(path: str, size: Optional[Tuple[int, int]] = None) -> Optional[Image.Image]:
        """Load an image with alpha channel, optionally resizing it."""
        try:
            img = Image.open(path).convert("RGBA")
            if size:
                img = img.resize(size, Image.Resampling.LANCZOS)
            return img
        except Exception as e:
            return None
    
    @staticmethod
    def crop(img: Image.Image, left: int = 0, top: int = 0, 
             right: int = 0, bottom: int = 0) -> Image.Image:
        """Crop an image from all sides."""
        w, h = img.size
        return img.crop((left, top, w - right, h - bottom))


# ============================================================================
# ORIGINAL PARSER CLASSES
# ============================================================================

class GovernmentParser:
    """Parses government definitions from governments.txt."""
    
    @staticmethod
    def parse(path: str) -> Tuple[Dict[str, str], Dict[str, str]]:
        """Parse governments.txt and return flagtype and index mappings."""
        try:
            with open(path, "r", encoding="latin-1") as f:
                text = f.read()
        except Exception:
            return {}, {}
        
        text = re.sub(r'#.*', '', text)
        
        gov_to_flagtype = {}
        index_to_govname = {}
        index = 0
        
        pattern = re.compile(r'([A-Za-z0-9_]+)\s*=\s*\{', re.M)
        pos = 0
        
        while True:
            match = pattern.search(text, pos)
            if not match:
                break
            
            name = match.group(1)
            block = GovernmentParser._extract_block(text, match.end() - 1)
            pos = match.end() + len(block)
            
            index += 1
            index_to_govname[str(index)] = name
            
            flagtype_match = re.search(r'flagType\s*=\s*([A-Za-z_]+)', block)
            if flagtype_match:
                gov_to_flagtype[name] = flagtype_match.group(1)
        
        return gov_to_flagtype, index_to_govname
    
    @staticmethod
    def _extract_block(text: str, start: int) -> str:
        """Extract a brace-delimited block from text."""
        brace_count = 0
        pos = start
        block_start = None
        
        while pos < len(text):
            if text[pos] == '{':
                brace_count += 1
                if brace_count == 1:
                    block_start = pos
            elif text[pos] == '}':
                brace_count -= 1
                if brace_count == 0 and block_start is not None:
                    return text[block_start:pos + 1]
            pos += 1
        
        return ''

class LocalizationParser:
    """Parses localization files for country names."""
    
    @staticmethod
    def parse_localization_files(state: AppState) -> Dict[str, str]:
        """Parse localization with proper multiple mod load order support."""
        if state._localization_cache is not None:
            return state._localization_cache
            
        country_names = {}
        loc_path = "localisation"
        
        # Get all files in the correct load order (last mod wins)
        all_files = []
        
        # BASE files first (lowest priority)
        if state.vic2_path:
            base_loc_path = Path(state.vic2_path) / loc_path
            if base_loc_path.exists():
                base_files = []
                for root, dirs, files in os.walk(str(base_loc_path)):
                    for file in files:
                        if file.lower().endswith('.csv'):
                            full_path = os.path.join(root, file)
                            base_files.append(full_path)
                base_files.sort()
                all_files.extend(base_files)
        
        # MOD files in REVERSE order (so last mod in list has highest priority)
        if state.mod_names and state.vic2_path:
            # Process mods in the order they appear in mod_names (first mod = lowest priority)
            for mod_name in state.mod_names:
                mod_loc_path = Path(state.vic2_path) / "mod" / mod_name / loc_path
                if mod_loc_path.exists():
                    mod_files = []
                    for root, dirs, files in os.walk(str(mod_loc_path)):
                        for file in files:
                            if file.lower().endswith('.csv'):
                                full_path = os.path.join(root, file)
                                mod_files.append(full_path)
                    mod_files.sort()
                    all_files.extend(mod_files)
        
        # Track what we've found for each category
        found_simple_country_tags = set()  # "ARY", "ARA" - basic country names
        found_government_tags = set()      # "ARY_absolute_monarchy" - government-specific names
        
        # Process ALL files in order (later files override earlier ones)
        for file_path in all_files:
            try:
                encodings = ['utf-8-sig', 'latin-1', 'cp1252', 'iso-8859-1', 'utf-8']
                content = None
                
                for encoding in encodings:
                    try:
                        with open(file_path, 'r', encoding=encoding) as f:
                            content = f.read()
                        break
                    except UnicodeDecodeError:
                        continue
                
                if content is None:
                    continue
                    
            except Exception:
                continue
            
            lines = content.split('\n')
            
            for line in lines:
                line = line.strip()
                if not line or line.startswith('#'):
                    continue
                
                separators = [';', ',']
                parts = None
                
                for sep in separators:
                    if sep in line:
                        parts = line.split(sep)
                        if len(parts) >= 2:
                            break
                
                if not parts or len(parts) < 2:
                    continue
                
                key = parts[0].strip().strip('"')
                value = parts[1].strip().strip('"')
                
                if key and value:
                    # CATEGORY 1: Simple country tags (like "ARY", "ARA")
                    is_simple_country_tag = (
                        len(key) == 3 and 
                        key.isalpha() and 
                        key.isupper()
                    )
                    
                    # CATEGORY 2: Government-specific country names (like "ARY_absolute_monarchy")
                    is_government_tag = (
                        '_' in key and 
                        len(key.split('_')[0]) == 3 and 
                        key.split('_')[0].isalpha() and 
                        key.split('_')[0].isupper()
                    )
                    
                    # CATEGORY 3: Everything else (parties, adjectives, etc.)
                    
                    if is_simple_country_tag:
                        # Simple country name - ALWAYS override (last mod wins)
                        country_names[key] = value
                        found_simple_country_tags.add(key)
                        
                        # Store lowercase version
                        country_names[key.lower()] = value
                    
                    elif is_government_tag:
                        # Government-specific country name - ALWAYS override (last mod wins)
                        country_names[key] = value
                        found_government_tags.add(key)
                        
                        # Store lowercase version
                        country_names[key.lower()] = value
                    
                    else:
                        # Everything else (parties, adjectives, etc.) - ALWAYS override
                        country_names[key] = value
                        if key != key.lower():
                            country_names[key.lower()] = value
                        if '_' in key:
                            simplified_key = key.replace('_', '')
                            country_names[simplified_key] = value
        
        state._localization_cache = country_names
        return country_names
    
    @staticmethod
    def _parse_localization_folder(folder_path: str, country_names: Dict[str, str]):
        """Parse CSV files in consistent alphabetical order."""
        if not os.path.isdir(folder_path):
            return
        
        csv_files = []
        for root, dirs, files in os.walk(folder_path):
            for file in files:
                if file.lower().endswith('.csv'):
                    csv_files.append(os.path.join(root, file))
        
        # Sort files alphabetically for consistent processing order
        csv_files.sort()
        
        for csv_file in csv_files:
            LocalizationParser._parse_csv_file(csv_file, country_names)
    
    @staticmethod
    def _parse_csv_file(file_path: str, country_names: Dict[str, str]):
        """Parse a single CSV file - ORIGINAL WORKING VERSION."""
        try:
            encodings = ['utf-8-sig', 'latin-1', 'cp1252', 'iso-8859-1', 'utf-8']
            content = None
            
            for encoding in encodings:
                try:
                    with open(file_path, 'r', encoding=encoding) as f:
                        content = f.read()
                    break
                except UnicodeDecodeError:
                    continue
            
            if content is None:
                return
                
        except Exception:
            return
        
        lines = content.split('\n')
        
        for line in lines:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            
            # Handle different CSV formats
            separators = [';', ',']
            parts = None
            
            for sep in separators:
                if sep in line:
                    parts = line.split(sep)
                    if len(parts) >= 2:
                        break
            
            if not parts or len(parts) < 2:
                continue
            
            key = parts[0].strip().strip('"')
            value = parts[1].strip().strip('"')
            
            if key and value:
                # Store both original key and lowercase version for case-insensitive lookup
                country_names[key] = value
                
                # Also store lowercase key for case-insensitive matching
                if key != key.lower():
                    country_names[key.lower()] = value
                
                # For government-specific names, also store without underscores for fuzzy matching
                if '_' in key:
                    simplified_key = key.replace('_', '')
                    country_names[simplified_key] = value
    
    @staticmethod
    def get_country_name(state: AppState, tag: str, government: Optional[str] = None) -> str:
        """Get the proper country name for a tag and government with fallbacks."""
        if not hasattr(state, 'localization_names') or not state.localization_names:
            state.localization_names = LocalizationParser.parse_localization_files(state)
        
        # Try government-specific name first (e.g., RUS_absolute_monarchy)
        if government:
            # Try various government name formats
            gov_keys_to_try = [
                f"{tag}_{government}",
                f"{tag}_{government.lower()}",
                f"{tag.upper()}_{government}",
                f"{tag.upper()}_{government.lower()}",
                # Also try without underscores for some mods
                f"{tag}{government}",
                f"{tag}{government.lower()}",
            ]
            
            for gov_key in gov_keys_to_try:
                if gov_key in state.localization_names:
                    return state.localization_names[gov_key]
        
        # Try regular tag name
        lookup_keys = [tag, tag.upper(), tag.lower(), f"_{tag}", f"_{tag.upper()}"]
        for key in lookup_keys:
            if key in state.localization_names:
                return state.localization_names[key]
        
        return tag

class SavefileParser:
    """Parses Victoria 2 save files for war data."""

    @staticmethod
    def _extract_block(text: str, start: int) -> str:
        """Extract a brace-delimited block."""
        brace_count = 0
        pos = start
        block_start = None
        
        while pos < len(text):
            if text[pos] == '{':
                brace_count += 1
                if brace_count == 1:
                    block_start = pos
            elif text[pos] == '}':
                brace_count -= 1
                if brace_count == 0 and block_start is not None:
                    return text[block_start:pos + 1]
            pos += 1
        
        return ''

# ============================================================================
# ANALYSIS TOOLS
# ============================================================================

class WarAnalyzer:
    """Enhanced war analysis tools."""
    
    @staticmethod
    def calculate_war_statistics(war: War) -> WarStatistics:
        """Calculate war score with casualty adjustment."""
        stats = WarStatistics(total_battles=len(war.battles))
        
        if not war.battles:
            stats.war_score_estimate = 0.0
            return stats
        
        # Count wins and losses
        stats.attacker_wins = sum(1 for battle in war.battles if battle.result is True)
        stats.defender_wins = sum(1 for battle in war.battles if battle.result is False)
        total_battles = len(war.battles)
        
        # Calculate total casualties
        stats.total_casualties["attacker"] = sum(battle.attacker.get("losses", 0) for battle in war.battles)
        stats.total_casualties["defender"] = sum(battle.defender.get("losses", 0) for battle in war.battles)
        
        # Base score from battle wins (-100% to +100%)
        base_score = ((stats.attacker_wins - stats.defender_wins) / total_battles) * 100
        
        # Casualty adjustment (-20% to +20% max)
        attacker_losses = stats.total_casualties.get("attacker", 0)
        defender_losses = stats.total_casualties.get("defender", 0)
        total_losses = attacker_losses + defender_losses
        
        if total_losses > 0 and attacker_losses > 0:
            casualty_ratio = defender_losses / attacker_losses
            # casualty_ratio > 1 = good for attacker (defender suffered more)
            # casualty_ratio < 1 = bad for attacker (attacker suffered more)
            casualty_adjustment = (casualty_ratio - 1) * 20  # Scale to ±20%
            casualty_adjustment = max(-20, min(20, casualty_adjustment))
        else:
            casualty_adjustment = 0
        
        # Combine scores
        stats.war_score_estimate = base_score + casualty_adjustment
        stats.war_score_estimate = max(-100, min(100, stats.war_score_estimate))
        
        return stats

class ToolTip:
    """Create a tooltip for a given widget."""
    def __init__(self, widget, text='', bg='#ffffe0', relief=tk.SOLID, borderwidth=1):
        self.widget = widget
        self.text = text
        self.bg = bg
        self.relief = relief
        self.borderwidth = borderwidth
        self.tip_window = None
        self.id = None
        self.x = self.y = 0

    def showtip(self, text, x, y):
        """Display text in tooltip window at specified coordinates."""
        self.text = text
        if self.tip_window or not self.text:
            return
        
      #  x = x + 10
      # y = y + 10
        
        self.tip_window = tw = tk.Toplevel(self.widget)
        tw.wm_overrideredirect(True)
        tw.wm_geometry(f"+{x}+{y}")
        
        label = tk.Label(tw, text=self.text, justify=tk.LEFT,
                        background=self.bg, relief=self.relief, 
                        borderwidth=self.borderwidth, font=("Arial", 10))
        label.pack(ipadx=1)

    def hidetip(self):
        """Hide the tooltip."""
        tw = self.tip_window
        self.tip_window = None
        if tw:
            tw.destroy()

def get_unit_icon(unit_strip_img, index):
    w = 36
    return unit_strip_img.crop((index * w, 0, (index + 1) * w, 36))

# ============================================================================
# UI COMPONENTS
# ============================================================================

# Remove the absolute paths and use relative paths like your other assets
class FontManager:
    """Manages fonts for the application with BMFont support including bold variants."""
    
    def __init__(self, state):
        self.state = state
        self._current_mods = None  # Track current mods
        self._initialize_fonts()
    
    def _initialize_fonts(self):
        """Initialize or reinitialize fonts when mods change."""
        self._current_mods = self.state.mod_names.copy() if self.state.mod_names else []
        
        # Define relative paths for BMFont files
        self.BMFONT_PATHS = {
            # Regular fonts
            10: os.path.join("gfx", "fonts", "Arial10.fnt"),
            12: os.path.join("gfx", "fonts", "Arial12.fnt"),
            14: os.path.join("gfx", "fonts", "garamond_14.fnt"),
            15: os.path.join("gfx", "fonts", "Arial14.fnt"),            
            16: os.path.join("gfx", "fonts", "garamond_16.fnt"),
            18: os.path.join("gfx", "fonts", "vic_18.fnt"),
            22: os.path.join("gfx", "fonts", "vic_22.fnt"),
            32: os.path.join("gfx", "fonts", "vic_32.fnt"),
            
            # Bold variants
            '12_bold': os.path.join("gfx", "fonts", "Arial12_bold.fnt"),
            '14_bold': os.path.join("gfx", "fonts", "garamond_14_bold.fnt"),
            '16_bold': os.path.join("gfx", "fonts", "garamond_16_bold.fnt"),
            '18_bold': os.path.join("gfx", "fonts", "vic_18_bold.fnt"),
            '22_bold': os.path.join("gfx", "fonts", "vic_22_bold.fnt"),
        }
        
        # Load fonts
        self.ttf_fonts = self._load_ttf_fonts()
        self.bmfonts = {}
        self._load_bmfonts()
    
    def _load_ttf_fonts(self):
        """Load TTF fonts for fallback with variants."""
        fonts = {}
        # Only the sizes we actually use
        sizes = [18, 22]
        
        for size in sizes:
            try:
                # Try to load regular
                regular_path = os.path.join("gfx", "fonts", f"fallback_{size}.ttf")
                full_path = self.state.get_modded_path(regular_path)
                if os.path.exists(full_path):
                    fonts[size] = ImageFont.truetype(full_path, size)
                else:
                    fonts[size] = ImageFont.load_default()
                
                # Try bold variant for size 22
                if size == 22:
                    bold_path = os.path.join("gfx", "fonts", f"fallback_{size}_bold.ttf")
                    bold_full_path = self.state.get_modded_path(bold_path)
                    if os.path.exists(bold_full_path):
                        fonts[f"{size}_bold"] = ImageFont.truetype(bold_full_path, size)
                    
            except:
                fonts[size] = ImageFont.load_default()
        
        return fonts
    
    def _load_bmfonts(self):
        """Load BMFonts for the specific sizes we need."""
        for variant, relative_path in self.BMFONT_PATHS.items():
            try:
                full_path = self.state.get_modded_path(relative_path)
                if os.path.exists(full_path):
                    bmfont = EasyFont(full_path)
                    self.bmfonts[variant] = bmfont
                else:
                    self.bmfonts[variant] = None
            except Exception as e:
                self.bmfonts[variant] = None
    
    def check_mods_changed(self):
        """Check if mods have changed and reload fonts if needed."""
        current_mods = self.state.mod_names.copy() if self.state.mod_names else []
        if current_mods != self._current_mods:
            print(f"Mods changed from {self._current_mods} to {current_mods}, reloading fonts...")
            self._initialize_fonts()
            return True
        return False
    
    def render_text(self, text: str, size: int = 18, color = "black", bold: bool = False) -> Image.Image:
        """Render text with mod change detection."""
        # Check for mod changes before rendering
        self.check_mods_changed()
        
        try:
            # Map requested sizes to available font sizes
            available_sizes = [10, 12, 14, 15, 16, 18, 22, 32]
            closest_size = min(available_sizes, key=lambda x: abs(x - size))
            variant_key = closest_size if not bold else f"{closest_size}_bold"
            
            if variant_key in self.bmfonts and self.bmfonts[variant_key] is not None:
                # Convert color
                if isinstance(color, tuple) and len(color) >= 3:
                    if color == (255, 255, 255):
                        easyfont_color = "white"
                    elif color == (0, 0, 0):
                        easyfont_color = "black"
                    else:
                        easyfont_color = color
                else:
                    easyfont_color = color

                result = self.bmfonts[variant_key].render(text, easyfont_color)
                return result
            else:
                return self._render_ttf_text(text, size, color, bold)
                
        except Exception as e:
            # Return a blank image as fallback
            return Image.new("RGBA", (10, 10), (0, 0, 0, 0))
    
    def _render_ttf_text(self, text: str, size: int, color: str, bold: bool = False) -> Image.Image:
        """Render text using TTF font with variants."""
        variant_key = size if not bold else f"{size}_bold"
        font = self.ttf_fonts.get(variant_key, self.ttf_fonts.get(size, self.ttf_fonts[18]))
        
        bbox = font.getbbox(text)
        text_width = bbox[2] - bbox[0]
        text_height = bbox[3] - bbox[1]
        
        padding = 4
        image = Image.new('RGBA', (text_width + padding, text_height + padding), (0, 0, 0, 0))
        draw = ImageDraw.Draw(image)
        
        draw.text((padding//2, padding//2), text, font=font, fill=color)
        return image
    
    def render_text_with_bg(self, text: str, size: int = 18, color: str = "black", 
                          bg_color: Tuple[int, int, int, int] = None,
                          bold: bool = False) -> Image.Image:
        """Render text with background and bold support."""
        text_image = self.render_text(text, size, color, bold)
        
        if bg_color is None:
            return text_image
        
        bg_image = Image.new('RGBA', text_image.size, bg_color)
        bg_image.paste(text_image, (0, 0), text_image)
        return bg_image
    
    def _color_name_to_rgb(self, color_name: str) -> Tuple[int, int, int]:
        """Convert color name to RGB tuple - ensure it's always 3 elements."""
        colors = {
            "black": (0, 0, 0),
            "white": (255, 255, 255),
            "red": (255, 0, 0),
            "green": (0, 255, 0),
            "blue": (0, 0, 255),
            "yellow": (255, 255, 0),
            "gray": (128, 128, 128),
            "grey": (128, 128, 128),
            "gold": (255, 215, 0),
            "silver": (192, 192, 192),
            "dark_red": (139, 0, 0),
            "dark_green": (0, 100, 0),
            "dark_blue": (0, 0, 139),
        }
        rgb = colors.get(color_name.lower(), (0, 0, 0))
        
        # Ensure we always return exactly 3 elements
        if len(rgb) == 3:
            return rgb
        elif len(rgb) == 1:
            return (rgb[0], rgb[0], rgb[0])  # Convert grayscale to RGB
        else:
            return (0, 0, 0)  # Fallback

    # Convenience methods
    def render_bold_text(self, text: str, size: int = 22, color: str = "white") -> Image.Image:
        """Convenience method for bold text (only works for size 22)."""
        #if size != 22:
        #    size = 22
        return self.render_text(text, size, color, bold=True)

    # Backward compatibility properties - only the ones we need
    @property
    def font_18(self):
        return self.ttf_fonts[18]
    
    @property
    def font_22(self):
        return self.ttf_fonts[22]
    
    # Tkinter font properties - only the ones we need
    @property
    def tk_font_18(self):
        return self.tk_fonts[18]
    
    @property
    def tk_font_22(self):
        return self.tk_fonts[22]
    
    @property
    def tk_font_22_bold(self):
        return self.tk_fonts['22_bold']

class EnhancedImageCache(ImageCache):
    """Enhanced image cache with memory monitoring and automatic cleanup."""
    
    def __init__(self, max_size: int = 1000):
        super().__init__(max_size)
        self.memory_usage = 0
        self.max_memory_mb = 500  # 500MB limit
    
    def estimate_memory_usage(self, image: Image.Image) -> int:
        """Estimate memory usage of an image in bytes."""
        if image.mode == 'RGBA':
            bytes_per_pixel = 4
        elif image.mode == 'RGB':
            bytes_per_pixel = 3
        else:
            bytes_per_pixel = 1  # Conservative estimate
        
        return image.width * image.height * bytes_per_pixel
    
    def set(self, key: str, image: Image.Image):
        """Set cache item with memory tracking."""
        image_size = self.estimate_memory_usage(image)
        
        # Check if we're approaching memory limits
        if self.memory_usage + image_size > self.max_memory_mb * 1024 * 1024:
            self.clear()  # Aggressive cleanup
        
        super().set(key, image)
        self.memory_usage += image_size
    
    def clear(self):
        """Clear cache and reset memory usage."""
        super().clear()
        self.memory_usage = 0

class MemoryManager:
    """Manages application memory usage and performs cleanup."""
    
    def __init__(self, state: AppState):
        self.state = state
        self.last_cleanup_time = 0
        self.cleanup_interval = 60  # Cleanup every 60 seconds
    
    def periodic_cleanup(self):
        """Perform periodic memory cleanup."""
        import time
        import gc
        
        current_time = time.time()
        if current_time - self.last_cleanup_time > self.cleanup_interval:
            self.force_cleanup()
            self.last_cleanup_time = current_time
    
    def force_cleanup(self):
        """Force garbage collection and cache cleanup."""
        import gc
        
        # Clear war image cache (most volatile)
        self.state.war_image_cache.clear()
        
        # Clear image references that are no longer needed
        if hasattr(self, 'image_refs'):
            # Keep only UI elements, clear war-specific images
            ui_images = [img for img in self.image_refs if any(ui_key in str(img) for ui_key in ['layer_', 'bg_', 'button_'])]
            self.image_refs.clear()
            self.image_refs.extend(ui_images)
        
        # Force garbage collection
        gc.collect()

class TextRenderer:
    """Handles all text rendering through FontManager with caching."""
    
    def __init__(self, font_manager: FontManager):
        self.font_manager = font_manager
        self.text_cache = {}  # Cache for rendered text images
    
    def render_cached_text(self, text: str, size: int = 18, color: str = "black", 
                          bold: bool = False) -> ImageTk.PhotoImage:
        """Render text with caching to avoid duplicate renders."""
        cache_key = f"{text}_{size}_{color}_{bold}"
        
        if cache_key in self.text_cache:
            return self.text_cache[cache_key]
        
        # Render text using FontManager
        text_image = self.font_manager.render_text(text, size, color, bold)
        if text_image is None:
            # Fallback: create empty image
            text_image = Image.new("RGBA", (10, 10), (0, 0, 0, 0))
        
        # Crop transparent padding for proper positioning
        bbox = text_image.getbbox()
        if bbox:
            text_image = text_image.crop(bbox)
        
        photo = ImageTk.PhotoImage(text_image)
        self.text_cache[cache_key] = photo
        return photo
    
    def create_text_element(self, canvas, text: str, x: int, y: int, 
                          size: int = 18, color: str = "black", bold: bool = False,
                          anchor: str = "nw") -> int:
        """Create a text element on canvas with proper memory management."""
        photo = self.render_cached_text(text, size, color, bold)
        return canvas.create_image(x, y, anchor=anchor, image=photo)
    
    def clear_cache(self):
        """Clear the text cache."""
        self.text_cache.clear()

class LayerCache:
    """Caches pre-rendered UI layers for each tab with dual cache system."""
    
    def __init__(self, state: AppState):
        self.state = state
        self.cache: Dict[str, List[Tuple[ImageTk.PhotoImage, Tuple[int, int]]]] = {}
        self.image_refs = []
    
    def build(self, tab_definitions: Dict):
        """Build layer cache for all tabs with dual cache system."""
        self.cache.clear()
        self.image_refs.clear()
        
        for tab_name, tab_data in tab_definitions.items():
            self.cache[tab_name] = []
            
            for layer in tab_data.get("layers", []):
                img = self._process_layer(layer)
                if img:
                    photo = ImageTk.PhotoImage(img)
                    self.image_refs.append(photo)
                    self.cache[tab_name].append((photo, layer["position"]))
    
    def _process_layer(self, layer: Dict) -> Optional[Image.Image]:
        """Process a single layer definition - ALWAYS use main cache for UI."""
        rel_path = os.path.join("gfx", "interface", layer["path"])
        full_path = self.state.get_modded_path(rel_path)
        
        # ALL LAYERS GO TO MAIN CACHE (persistent UI elements)
        cache_key = f"layer_{layer['path']}"
        cached_img = self.state.image_cache.get(cache_key)
        if cached_img:
            img = cached_img
        else:
            img = SafeLoader.safe_load_image(full_path)
            if img:
                self.state.image_cache.set(cache_key, img)  # Always main cache
        
        if not img:
            return None
        
        if "crop" in layer:
            img = ImageLoader.crop(img, **layer["crop"])
        
        if "size" in layer:
            img = img.resize(layer["size"], Image.Resampling.LANCZOS)
        
        return img
    
    def get(self, tab_name: str) -> List[Tuple[ImageTk.PhotoImage, Tuple[int, int]]]:
        """Get cached layers for a tab."""
        return self.cache.get(tab_name, [])
    
    def clear(self):
        """Clear cache and image references."""
        self.cache.clear()
        self.image_refs.clear()

class ScrollableList:
    """A scrollable list widget with custom scrollbar."""
    
    def __init__(self, parent_canvas: tk.Canvas, root: tk.Tk, state: AppState,
                 x: int, y: int, width: int, height: int, scrollbar_assets: Dict,
                 row_height: int = 24):
        self.parent_canvas = parent_canvas
        self.root = root
        self.state = state
        self.x = x
        self.y = y
        self.width = width
        self.height = height
        self.sb_assets = scrollbar_assets
        self.row_height = row_height
        self.scrollbar_images = []
        
        self.content_width = width - SCROLLBAR_WIDTH - 4
        self.scroll_canvas = None
        self.frame_inside = None
        self.overlay_ids = []
        self.thumb_state = {"height": 16, "photo": None, "base": scrollbar_assets.get("thumb_img")}
        self.drag_state = {"active": False, "offset": 0}
        
    def create(self):
        """Create the scrollable list."""
        self._clear_previous()
        self._create_scroll_canvas()
        self._create_scrollbar()
        self.root.after(50, self._update_thumb)
    
    def _clear_previous(self):
        """Clear previous overlay elements."""
        for overlay_id in self.overlay_ids:
            try:
                self.parent_canvas.delete(overlay_id)
            except Exception:
                pass
        self.overlay_ids.clear()
    
    def _create_scroll_canvas(self):
        """Create the scrollable canvas and inner frame."""
        self.scroll_canvas = tk.Canvas(self.root, width=self.content_width, height=self.height,
                                      bg=BG_COLOR, highlightthickness=0)
        
        self.frame_inside = tk.Frame(self.scroll_canvas, bg=BG_COLOR)
        self.scroll_canvas.create_window((0, 0), window=self.frame_inside, anchor="nw")
        
        self.scroll_canvas.configure(yscrollincrement=self.row_height)
        self.frame_inside.bind("<Configure>", self._on_configure)
        self.scroll_canvas.configure(yscrollcommand=lambda f, l: self._update_thumb())
        
        win_id = self.parent_canvas.create_window(self.x, self.y, anchor=tk.NW, window=self.scroll_canvas)
        self.overlay_ids.append(win_id)
        
        self.scroll_canvas.bind("<Enter>", lambda e: self.scroll_canvas.bind_all("<MouseWheel>", self._on_mousewheel))
        self.scroll_canvas.bind("<Leave>", lambda e: self.scroll_canvas.unbind_all("<MouseWheel>"))
    
    def _create_scrollbar(self):
        """Create the custom scrollbar."""
        sb_x = self.x + (self.width - SCROLLBAR_WIDTH) - 4
        sb_y = self.y
        track_top = sb_y + 16
        track_bottom = sb_y + self.height - 16
        track_height = track_bottom - track_top
        
        self.track_top = track_top
        self.track_bottom = track_bottom
        self.track_height = track_height
        self.sb_x = sb_x
        
        # Track tiles
        if 'track_tile' in self.sb_assets:
            tile = self.sb_assets['track_tile']
            tile_height = tile.size[1]
            for y_pos in range(track_top, track_bottom, tile_height):
                tile_photo = ImageTk.PhotoImage(tile)
                self.scrollbar_images.append(tile_photo)
                tile_id = self.parent_canvas.create_image(sb_x + 5, y_pos, anchor=tk.NW, image=tile_photo)
                self.overlay_ids.append(tile_id)
        
        track_rect = self.parent_canvas.create_rectangle(sb_x, track_top, sb_x + SCROLLBAR_WIDTH, track_bottom,
                                                        outline="", fill="", tags=("scroll_track",))
        self.overlay_ids.append(track_rect)
        
        # Up/down buttons
        if 'up' in self.sb_assets:
            up_photo = self.sb_assets['up']
            up_id = self.parent_canvas.create_image(sb_x, sb_y, anchor=tk.NW, image=up_photo)
        else:
            up_id = self.parent_canvas.create_rectangle(sb_x, sb_y, sb_x + SCROLLBAR_WIDTH, sb_y + 16, 
                                                       outline="#888", fill="#888")
        
        if 'down' in self.sb_assets:
            down_photo = self.sb_assets['down']
            down_id = self.parent_canvas.create_image(sb_x, sb_y + self.height - 16, anchor=tk.NW, image=down_photo)
        else:
            down_id = self.parent_canvas.create_rectangle(sb_x, sb_y + self.height - 16, sb_x + SCROLLBAR_WIDTH, 
                                                         sb_y + self.height, outline="#888", fill="#888")
        
        self.overlay_ids.extend([up_id, down_id])
        
        # Thumb
        thumb_photo = self._make_thumb_photo(16) if self.thumb_state["base"] else None
        if thumb_photo:
            self.thumb_id = self.parent_canvas.create_image(sb_x, track_top, anchor=tk.NW, image=thumb_photo)
        else:
            self.thumb_id = self.parent_canvas.create_rectangle(sb_x, track_top, sb_x + SCROLLBAR_WIDTH, track_top + 16, 
                                                               outline="#666", fill="#666")
        
        self.overlay_ids.append(self.thumb_id)
        self.thumb_state["photo"] = thumb_photo
        
        # Bind events
        self.parent_canvas.tag_bind(up_id, "<Button-1>", 
            lambda e: (self.scroll_canvas.yview_scroll(-1, "units"), self._update_thumb()))
        self.parent_canvas.tag_bind(down_id, "<Button-1>", 
            lambda e: (self.scroll_canvas.yview_scroll(1, "units"), self._update_thumb()))
        self.parent_canvas.tag_bind("scroll_track", "<Button-1>", self._on_track_click)
        self.parent_canvas.tag_bind(self.thumb_id, "<Button-1>", self._thumb_press)
        self.parent_canvas.tag_bind(self.thumb_id, "<B1-Motion>", self._thumb_motion)
        self.parent_canvas.tag_bind(self.thumb_id, "<ButtonRelease-1>", self._thumb_release)
    
    def _on_configure(self, event=None):
        """Update scroll region when content changes."""
        bbox = self.scroll_canvas.bbox("all")
        if bbox:
            self.scroll_canvas.configure(scrollregion=bbox)
        self._update_thumb()
    
    def _make_thumb_photo(self, height: int) -> Optional[ImageTk.PhotoImage]:
        """Create a thumb photo."""
        base = self.thumb_state["base"]
        if base is None:
            return None
        img = base.resize((16, 16), Image.Resampling.NEAREST)
        return ImageTk.PhotoImage(img)
    
    def _set_thumb_height(self, height: int):
        """Set the thumb to a specific height."""
        height = max(16, min(height, self.track_height))
        if self.thumb_state["base"] is None:
            coords = self.parent_canvas.coords(self.thumb_id)
            self.parent_canvas.coords(self.thumb_id, self.sb_x, coords[1],
                                     self.sb_x + SCROLLBAR_WIDTH, self.track_top + height)
            self.thumb_state["height"] = height
            return
        
        if height == self.thumb_state["height"]:
            return
        
        if self.thumb_state["photo"] is None:
            new_photo = self._make_thumb_photo(16)
            self.thumb_state["photo"] = new_photo
            self.parent_canvas.itemconfig(self.thumb_id, image=new_photo)
        
        self.thumb_state["height"] = height
    
    def _update_thumb(self):
        """Update thumb position and size based on scroll position."""
        bbox = self.scroll_canvas.bbox("all")
        if not bbox:
            self._set_thumb_height(self.track_height)
            self.parent_canvas.coords(self.thumb_id, self.sb_x, self.track_top)
            return
        
        x1, y1, x2, y2 = bbox
        content_height = max(1, y2 - y1)
        
        if content_height <= self.height:
            self._set_thumb_height(self.track_height)
            self.parent_canvas.coords(self.thumb_id, self.sb_x, self.track_top)
            return
        
        visible_fraction = self.height / content_height
        thumb_height = max(20, int(visible_fraction * self.track_height))
        thumb_height = min(thumb_height, self.track_height)
        self._set_thumb_height(thumb_height)
        
        first, last = self.scroll_canvas.yview()
        travel = self.track_height - thumb_height
        thumb_y = self.track_top + int(float(first) * max(0, travel))
        self.parent_canvas.coords(self.thumb_id, self.sb_x, thumb_y)
    
    def _on_mousewheel(self, event):
        """Handle mouse wheel scrolling."""
        if hasattr(event, 'delta') and event.delta:
            steps = int(-event.delta / 120)
            self.scroll_canvas.yview_scroll(steps, "units")
        elif hasattr(event, 'num'):
            if event.num == 4:
                self.scroll_canvas.yview_scroll(-1, "units")
            elif event.num == 5:
                self.scroll_canvas.yview_scroll(1, "units")
        self._update_thumb()
    
    def _on_track_click(self, event):
        """Handle clicks on the scrollbar track."""
        y = event.y
        thumb_height = max(20, self.thumb_state["height"])
        travel = max(1, self.track_height - thumb_height)
        y_clamped = max(self.track_top, min(y - thumb_height // 2, self.track_bottom - thumb_height))
        fraction = (y_clamped - self.track_top) / travel
        fraction = max(0.0, min(1.0, fraction))
        
        bbox = self.scroll_canvas.bbox("all")
        if bbox:
            content_height = max(1, bbox[3] - bbox[1])
            visible_rows = self.height / self.row_height
            total_rows = content_height / self.row_height
            row_index = int(fraction * (total_rows - visible_rows))
            self.scroll_canvas.yview_moveto(row_index * self.row_height / content_height)
        
        self._update_thumb()
    
    def _thumb_press(self, event):
        """Handle thumb press for dragging."""
        self.drag_state["active"] = True
        x, y = self.parent_canvas.coords(self.thumb_id)
        self.drag_state["offset"] = event.y - int(y)
    
    def _thumb_motion(self, event):
        """Handle thumb drag motion."""
        if not self.drag_state["active"]:
            return
        
        thumb_height = max(20, self.thumb_state["height"])
        travel = max(1, self.track_height - thumb_height)
        new_y = max(self.track_top, min(event.y - self.drag_state["offset"], self.track_bottom - thumb_height))
        
        self.parent_canvas.coords(self.thumb_id, self.sb_x, new_y)
        fraction = (new_y - self.track_top) / travel
        self.scroll_canvas.yview_moveto(fraction)
    
    def _thumb_release(self, event):
        """Handle thumb release after dragging."""
        self.drag_state["active"] = False
        self._update_thumb()


class WarAnalyzerGUI:
    """Main GUI application for the war analyzer."""
    
    def __init__(self, state: AppState):
        self.state = state
        self.root = tk.Toplevel()
        self.root.title("Victoria 2 GUI War Analyzer")
        self.root.geometry(f"{WINDOW_WIDTH}x{WINDOW_HEIGHT}")
        self.root.resizable(False, False)
        self.state.image_cache = EnhancedImageCache(max_size=800)
        self.state.war_image_cache = EnhancedImageCache(max_size=400)
        # Initialize memory manager
        self.memory_manager = MemoryManager(self.state)
        
        # Initialize text renderer
        self.font_manager = FontManager(self.state)
        self.text_renderer = TextRenderer(self.font_manager)
        self.layer_cache = LayerCache(state)
        
        # Schedule periodic cleanup
        self.root.after(30000, self._periodic_maintenance)  # Every 30 seconds

        # Use your existing mod-aware path resolution
        vic2_icon_path = self.state.get_modded_path("Victoria2.ico")
        if os.path.exists(vic2_icon_path):
            self.root.iconbitmap(vic2_icon_path)
        else:
            # Fallback: try the base Victoria 2 directory
            base_icon_path = os.path.join(self.state.vic2_path, "Victoria2.ico")
            if os.path.exists(base_icon_path):
                self.root.iconbitmap(base_icon_path)
            else:
                print("Victoria2.ico not found in any location")
        
        self.state.config = AppConfig.load()
        self.canvas = tk.Canvas(self.root, width=WINDOW_WIDTH, height=WINDOW_HEIGHT, highlightthickness=0)
        self.canvas.pack()
        self.font_manager = FontManager(self.state)
        self.layer_cache = LayerCache(state)
        self.current_tab = "Tab1"
        self.image_refs = []
        self.tooltip = ToolTip(self.canvas)
        self.war_image_cache = ImageCache(max_size=200)
        # Add this flag to track if auto-load has already run
        self.auto_load_executed = False
        self.tab_text_images = []  # Separate list for tab text images only
        # Initialize CB icons
        self._cb_icons = {}
        self._cb_icons_loaded = False
        self.cb_to_sprite = {}
        
        self.scrollbar_assets = self._load_scrollbar_assets()
        self.tab_definitions = self._create_tab_definitions()
        
        self._load_government_data()
        self.layer_cache.build(self.tab_definitions)
        
        # Pre-load CB icons
        self._load_cb_icons()
        
        self._draw_tab_content(self.current_tab)
        self.root.protocol("WM_DELETE_WINDOW", self.cleanup)
        self._create_status_bar()
        
        # Auto-load last file and mod on startup if enabled
        if self.state.config.auto_load_last and not self.auto_load_executed:
            self._schedule_auto_load()

    def _periodic_maintenance(self):
        """Perform periodic maintenance tasks."""
        try:
            self.memory_manager.periodic_cleanup()
            
            # Clear text cache if it gets too large
            if len(self.text_renderer.text_cache) > 1000:
                self.text_renderer.clear_cache()
            
        except Exception as e:
            print(f"Maintenance error: {e}")
        
        # Schedule next maintenance
        self.root.after(30000, self._periodic_maintenance)

    def _schedule_auto_load(self):
        """Schedule auto-load to run once after a short delay."""
        if self.auto_load_executed:
            return
            
        # Load last mods
        if self.state.config.last_mods:
            self.state.mod_names = self.state.config.last_mods.copy()
            # Update the mod label if it exists
            if hasattr(self, 'mod_label'):
                mod_display = ', '.join(self.state.mod_names) if self.state.mod_names else 'None'
                self.mod_label.config(text=f"Current Mods: {mod_display}")
        
        # Load last file
        if self.state.config.recent_files:
            last_file = self.state.config.recent_files[0]
            if os.path.exists(last_file):
                self.state.save_file_path = last_file
                # Update the save label if it exists - WITH FORMATTING
                if hasattr(self, 'save_label'):
                    filename = os.path.basename(last_file)
                    folder1 = os.path.basename(os.path.dirname(last_file))  # Immediate parent
                    folder2 = os.path.basename(os.path.dirname(os.path.dirname(last_file)))  # Grandparent
                    display_name = f"{folder2}/{folder1}/{filename}"
                    self.save_label.config(text=display_name)

    def _load_recent_file(self, file_path):
        """Load a file from the recent files list."""
        if os.path.exists(file_path):
            self.state.save_file_path = file_path
            
            # Apply the same path formatting
            filename = os.path.basename(file_path)
            folder1 = os.path.basename(os.path.dirname(file_path))  # Immediate parent
            folder2 = os.path.basename(os.path.dirname(os.path.dirname(file_path)))  # Grandparent
            display_name = f"{folder2}/{folder1}/{filename}"
            self.save_label.config(text=display_name)
            
            # Move to top of recent files
            if file_path in self.state.config.recent_files:
                self.state.config.recent_files.remove(file_path)
            self.state.config.recent_files.insert(0, file_path)
            self.state.config.save()
            self._refresh_recent_files()
            
            # Actually load the file
            self.load_save()
        else:
            # Remove non-existent file from recent list
            if file_path in self.state.config.recent_files:
                self.state.config.recent_files.remove(file_path)
                self.state.config.save()
                self._refresh_recent_files()
            messagebox.showwarning("File not found", f"The file no longer exists:\n{file_path}")

    def _refresh_recent_files(self):
        """Refresh the recent files display."""
        if hasattr(self, 'recent_file_buttons'):
            for btn in self.recent_file_buttons:
                try:
                    btn.destroy()
                except:
                    pass
            self.recent_file_buttons.clear()
            
        recent_y = 350
        for i, file_path in enumerate(self.state.config.recent_files):
            if os.path.exists(file_path):
                filename = os.path.basename(file_path)
                btn = tk.Label(self.root, text=f"{i+1}. {filename}", 
                            cursor="hand2", bg=BG_COLOR, fg="blue", font=("Arial", 10))
                btn.bind("<Button-1>", lambda e, path=file_path: self._load_recent_file(path))
                self.canvas.create_window(30, recent_y, anchor=tk.NW, window=btn)
                self.recent_file_buttons.append(btn)
                recent_y += 25

    def _auto_load_file(self, file_path):
        """Automatically load a save file on startup."""
        try:
            self.auto_load_executed = True
            self.state.save_file_path = file_path
            
            # Reload assets with the correct mod first
            if self.state.config.last_mod and self.state.config.last_mod != "None":
                self._reload_assets()
                self._reload_cb_icons()
            
            # Use the existing load_save logic but without message boxes for auto-load
            if not self.state._wars_data or not self.state.save_file_text:  # Changed to save_file_text
                self._load_government_data()
                
                wars, full_text = OptimizedSavefileParser.parse_wars(self.state.save_file_path)
                self.state._wars_data = wars
                self.state.save_file_text = full_text  # Set the PUBLIC field
                
                self.state._parsed_country_cultures, self.state._parsed_country_governments = CultureLeaderMapper.detect_cultures_and_governments(
                    full_text, self.state.gov_index_map
                )
                
                self.state._parsed_great_nations = OptimizedSavefileParser.parse_great_nations(full_text)
                self.state.localization_names = LocalizationParser.parse_localization_files(self.state)
            
            self.state.filtered_wars = self.state._wars_data[:]
            self.state.selected_countries.clear()
            self.state.selected_war_index = None
            
            # Switch to wars tab after auto-load
            self._on_tab_click("Tab2")
            
        except Exception:
            # Silently handle auto-load failures
            pass

    def load_save(self):
        """Load the currently selected save file."""
        if not os.path.isfile(self.state.save_file_path):
            messagebox.showwarning("No file", "Please select a valid save file first.")
            return
        
        try:
            messagebox.showinfo("Loading", "Parsing save file... This may take a moment.")
            
            # Only parse if we haven't already
            if not self.state._wars_data or not self.state.save_file_text:
                self._load_government_data()
                self._reload_cb_icons()
                
                # Parse wars and full text
                wars, full_text = OptimizedSavefileParser.parse_wars(self.state.save_file_path)
                self.state._wars_data = wars
                self.state.save_file_text = full_text
                
                # Detect cultures and governments
                self.state._parsed_country_cultures, self.state._parsed_country_governments = CultureLeaderMapper.detect_cultures_and_governments(
                    full_text, self.state.gov_index_map
                )
                
                # Parse great nations
                self.state._parsed_great_nations = OptimizedSavefileParser.parse_great_nations(full_text)
                
                # Load localization
                self.state.localization_names = LocalizationParser.parse_localization_files(self.state)
            
            # Always reset filters and selection
            self.state.filtered_wars = self.state._wars_data[:]
            self.state.selected_countries.clear()
            self.state.selected_war_index = None
            
            self._on_tab_click("Tab2")
            messagebox.showinfo("Loaded", f"Loaded {len(self.state._wars_data)} wars and data for {len(self.state._parsed_country_governments)} countries.")
            
        except Exception as e:
            messagebox.showerror("Error", f"Failed to load save: {e}")
        
    def _create_tab_definitions(self) -> Dict:
        """Define all tab layouts."""
        base_layers = [
            {"path": "main_bg.dds", "position": (0, 0), "size": (342, WINDOW_HEIGHT),
             "crop": {"left": 71, "top": 65, "right": 75+680, "bottom": 66}},
            {"path": "main_bg.dds", "position": (342, 0), "size": (342, WINDOW_HEIGHT),
             "crop": {"left": 71+680, "top": 65, "right": 75, "bottom": 66}},
            {"path": "diplomacy_bg.dds", "position": (17, 14),
             "size": (988-338, 616), "crop": {"left": 0, "top": 0, "right": 338, "bottom": 0}},             
        ]
        
        return {
            "Tab1": {
                "label": "Settings",
                "layers": base_layers + [
                    {"path": "politics_tab_button.dds", "position": (24, 18), "size": (154, 36),
                     "crop": {"left": 154, "top": 0, "right": 0, "bottom": 0}},
                    {"path": "politics_tab_button.dds", "position": (184, 18), "size": (154, 36),
                     "crop": {"left": 0, "top": 0, "right": 154, "bottom": 0}},
                    {"path": "politics_tab_button.dds", "position": (344, 18), "size": (154, 36),
                     "crop": {"left": 0, "top": 0, "right": 154, "bottom": 0}},
                    {"path": "politics_tab_button.dds", "position": (504, 18), "size": (154, 36),
                     "crop": {"left": 0, "top": 0, "right": 154, "bottom": 0}},
                ],
            },
            "Tab2": {
                "label": "Show wars",
                "layers": base_layers + [
                    {"path": "politics_tab_button.dds", "position": (184, 18), "size": (154, 36),
                     "crop": {"left": 154, "top": 0, "right": 0, "bottom": 0}},
                    {"path": "politics_tab_button.dds", "position": (24, 18), "size": (154, 36),
                     "crop": {"left": 0, "top": 0, "right": 154, "bottom": 0}},
                    {"path": "politics_tab_button.dds", "position": (344, 18), "size": (154, 36),
                     "crop": {"left": 0, "top": 0, "right": 154, "bottom": 0}},
                    {"path": "politics_tab_button.dds", "position": (504, 18), "size": (154, 36),
                     "crop": {"left": 0, "top": 0, "right": 154, "bottom": 0}},
                ],
            },
            "Tab3": {
                "label": "War Details",
                "layers": base_layers + [
                    {"path": "politics_tab_button.dds", "position": (344, 18), "size": (154, 36),
                     "crop": {"left": 154, "top": 0, "right": 0, "bottom": 0}},
                    {"path": "politics_tab_button.dds", "position": (24, 18), "size": (154, 36),
                     "crop": {"left": 0, "top": 0, "right": 154, "bottom": 0}},
                    {"path": "politics_tab_button.dds", "position": (184, 18), "size": (154, 36),
                     "crop": {"left": 0, "top": 0, "right": 154, "bottom": 0}},
                    {"path": "politics_tab_button.dds", "position": (504, 18), "size": (154, 36),
                     "crop": {"left": 0, "top": 0, "right": 154, "bottom": 0}},
                ],
            },
        }
    

    def _on_any_flag_enter(self, event, country_tag):
        """Handle mouse entering any flag."""
        if not hasattr(self, '_flag_tooltip_cache'):
            self._flag_tooltip_cache = {}
        
        if country_tag not in self._flag_tooltip_cache:
            gov = self.state.country_governments.get(country_tag, "Unknown")
            country_name = LocalizationParser.get_country_name(self.state, country_tag, gov)
            self._flag_tooltip_cache[country_tag] = country_name
        
        tooltip_text = self._flag_tooltip_cache[country_tag]
        
        # FIX: Just pass the screen coordinates directly, no canvas offset
        screen_x = event.x_root + 10  # Small offset from cursor
        screen_y = event.y_root + 10  # Small offset from cursor
        self.tooltip.showtip(tooltip_text, screen_x, screen_y)
        
        if hasattr(self, 'country_panel'):
            self.country_panel.update_country(country_tag)

    def _on_any_flag_motion(self, event, country_tag):
        """Handle mouse motion over any flag."""
        # FIX: Just pass the screen coordinates directly
        screen_x = event.x_root + 10  # Small offset from cursor
        screen_y = event.y_root + 10  # Small offset from cursor
        self.tooltip.showtip(self._flag_tooltip_cache.get(country_tag, ""), screen_x, screen_y)

    def _on_any_flag_leave(self, event):
        """Handle mouse leaving any flag."""
        self.tooltip.hidetip()
    
    def _cache_flag_tooltips(self, war):
        """Pre-cache all tooltip texts for flags in this war."""
        self._flag_tooltip_cache = {}
        
        # Cache all attacker flags
        for tag in war.attackers:
            if tag and tag != "---":
                gov = self.state.country_governments.get(tag, "Unknown")
                country_name = LocalizationParser.get_country_name(self.state, tag, gov)
                self._flag_tooltip_cache[tag] = country_name
        
        # Cache all defender flags  
        for tag in war.defenders:
            if tag and tag != "---":
                gov = self.state.country_governments.get(tag, "Unknown")
                country_name = LocalizationParser.get_country_name(self.state, tag, gov)
                self._flag_tooltip_cache[tag] = country_name

    def _load_scrollbar_assets(self) -> Dict:
        """Load scrollbar graphics."""
        assets = {}
        
        def crop_right_16(rel_path: str) -> Optional[Image.Image]:
            full_path = self.state.get_modded_path(os.path.join("gfx", "interface", rel_path))
            img = SafeLoader.safe_load_image(full_path)
            if not img:
                return None
            cropped = ImageLoader.crop(img, right=img.width - 16)
            return cropped.resize((16, 16), Image.Resampling.NEAREST)
        
        up_img = crop_right_16("scrollbar_upbutton.dds")
        down_img = crop_right_16("scrollbar_downbutton.dds")
        thumb_img = crop_right_16("scrollbar_slider_2.dds")
        
        if up_img:
            assets['up'] = ImageTk.PhotoImage(up_img)
            self.image_refs.append(assets['up'])
        if down_img:
            assets['down'] = ImageTk.PhotoImage(down_img)
            self.image_refs.append(assets['down'])
        if thumb_img:
            assets['thumb_img'] = thumb_img
            assets['thumb'] = ImageTk.PhotoImage(thumb_img)
            self.image_refs.append(assets['thumb'])
        
        track_path = self.state.get_modded_path(os.path.join("gfx", "interface", "scrollbar_sliderbackground_2.tga"))
        track = SafeLoader.safe_load_image(track_path)
        if track:
            tile = ImageLoader.crop(track, top=1)
            if tile and tile.width > 0 and tile.height > 0:
                assets['track_tile'] = tile
        
        return assets
    
    def _load_government_data(self):
        """Load government definitions with caching."""
        if (self.state._government_cache_loaded and 
            self.state.gov_to_flagtype and 
            self.state.gov_index_map):
            return  # Already loaded
        
        govs_path = self.state.get_modded_path(os.path.join("common", "governments.txt"))
        if os.path.exists(govs_path):
            gov_to_flag, gov_index = GovernmentParser.parse(govs_path)
            self.state.gov_to_flagtype = gov_to_flag
            self.state.gov_index_map = gov_index
            self.state._government_cache_loaded = True
    
    def _load_cb_icons(self):
        """Load and cache CB icons from the sprite sheet with proper mod support."""
        if hasattr(self, '_cb_icons_loaded') and self._cb_icons_loaded:
            return
        
        self._cb_icons = {}
        self._cb_icons_loaded = True
        
        # Parse CB types to get sprite indices
        self.cb_to_sprite = CBTypesParser.parse_cb_types(self.state)
        
        # Try to load from mod first, then base game
        icons_path = self.state.get_modded_path(os.path.join("gfx", "interface", "diplo_wargoal_icons.dds"))
        
        sprite_sheet = SafeLoader.safe_load_image(icons_path)
        
        if not sprite_sheet:
            return
        
        # Get sprite sheet dimensions
        sheet_width, sheet_height = sprite_sheet.size
        
        # Victoria 2 CB icons are ALWAYS 24px wide
        icon_width = 24
        icon_height = sheet_height  # Full height of the sprite sheet
        
        # Create icons for each CB type
        for cb_name, sprite_index in self.cb_to_sprite.items():
            # FIX: Victoria 2 sprite indices are 1-based, but we need 0-based for cropping
            # sprite_index=1 should be position 0-23, sprite_index=2 should be position 24-47, etc.
            x_pos = (sprite_index - 1) * icon_width
            
            # Make sure we don't go beyond the sprite sheet bounds
            if x_pos + icon_width <= sheet_width and x_pos >= 0:
                try:
                    # Extract the 24x(sheet_height) icon and ensure transparency
                    icon = sprite_sheet.crop((x_pos, 0, x_pos + icon_width, icon_height))
                    # Convert to RGBA if not already to ensure transparency
                    if icon.mode != 'RGBA':
                        icon = icon.convert('RGBA')
                    self._cb_icons[cb_name] = icon
                except Exception:
                    # Silently continue if one icon fails to load
                    pass

    def _get_cb_icon(self, cb_name: str) -> Optional[Image.Image]:
        """Get the CB icon with mod-aware reloading."""
        # Remove the complex mod change detection - keep it simple
        current_mod = self.state.mod_name
        if not hasattr(self, '_last_mod_loaded') or self._last_mod_loaded != current_mod:
            self._last_mod_loaded = current_mod
            self._cb_icons_loaded = False  # Just mark as not loaded
        
        if not hasattr(self, '_cb_icons') or not self._cb_icons_loaded:
            self._load_cb_icons()
        
        # Try exact match first
        if cb_name in self._cb_icons:
            icon = self._cb_icons[cb_name]
            # Ensure the icon has transparency
            if icon.mode != 'RGBA':
                icon = icon.convert('RGBA')
            return icon
        
        # Try case-insensitive match
        cb_name_lower = cb_name.lower()
        for stored_cb, icon in self._cb_icons.items():
            if stored_cb.lower() == cb_name_lower:
                if icon.mode != 'RGBA':
                    icon = icon.convert('RGBA')
                return icon
        
        # Try partial match (in case CB names have prefixes/suffixes)
        for stored_cb, icon in self._cb_icons.items():
            if cb_name_lower in stored_cb.lower() or stored_cb.lower() in cb_name_lower:
                if icon.mode != 'RGBA':
                    icon = icon.convert('RGBA')
                return icon
        
        return None

    def _extract_cb_name_from_goal(self, goal_text: str) -> Optional[str]:
        """Extract the CB name from goal description text with better matching."""
        if not goal_text:
            return None
        
        # Common Victoria 2 CB names - expanded list
        common_cbs = [
            'acquire_state', 'conquer_state', 'liberate_state', 'take_state',
            'free_people', 'free_substate', 'make_puppet', 'make_vassal',
            'annex', 'take_capital', 'humiliate', 'cut_down_to_size',
            'disarmament', 'destroy_forts', 'destroy_navies', 'war_reparations',
            'return_state', 'core_state', 'unification', 'cleansing_of_heretics',
            'crusade', 'holy_war', 'colonial_war', 'place_in_sun',
            'imperialism', 'containment', 'install_communist_gov',
            'install_fascist_gov', 'install_democratic_gov', 'add_to_sphere',
            'remove_from_sphere', 'assert_hegemony', 'crisis_war'
        ]
        
        goal_lower = goal_text.lower()
        
        # First, try direct matching with the CB names we have loaded
        for cb_name in self._cb_icons.keys():
            if cb_name.lower() in goal_lower:
                return cb_name
        
        # Then try with common CB names
        for cb in common_cbs:
            if cb in goal_lower:
                return cb
        
        # Try to extract from common patterns in goal text
        patterns = [
            r'(\w+)\s+against',  # "cb_name against"
            r'(\w+)\s+\(State',  # "cb_name (State"
            r'(\w+)\s+from',     # "cb_name from"  
            r'(\w+)\s+on',       # "cb_name on"
            r'^([A-Za-z_]+)\b',  # Start of string
        ]
        
        for pattern in patterns:
            match = re.search(pattern, goal_text)
            if match:
                potential_cb = match.group(1).lower()
                # Check if this matches any known CB
                for cb_name in self._cb_icons.keys():
                    if cb_name.lower() == potential_cb:
                        return cb_name
                
                for cb in common_cbs:
                    if cb == potential_cb:
                        return cb
        
        # Last resort: try partial matching
        words = re.findall(r'\b[a-z_]+\b', goal_lower)
        for word in words:
            for cb_name in self._cb_icons.keys():
                if word in cb_name.lower() or cb_name.lower() in word:
                    return cb_name
        
        return None

    def _draw_war_header_simple(self, war: War, x: int, y: int):
        """Draw simplified war header with defenders on the right side."""
        # War name - using FontManager with white text
        text_image = self.font_manager.render_bold_text(war.name, 22, "white")
        
        photo = ImageTk.PhotoImage(text_image)
        self.image_refs.append(photo)
        
        # Center the text
        text_x = x + 319
        text_y = y - 24
        
        name_id = self.canvas.create_image(text_x, text_y, anchor=tk.N, image=photo)
        self._tab3_widgets.append(name_id)
        
        # Load flag overlay for normal flags
        overlay_path = self.state.get_modded_path(os.path.join("gfx", "interface", "flag_overlay_new.tga"))
        flag_overlay = SafeLoader.safe_load_image(overlay_path, size=(FLAG_WIDTH, FLAG_HEIGHT))
        
        self._draw_simple_flag_row(war.attackers, war.original_attacker, x + 3, y + 50, flag_overlay, is_attacker=True)
        self._draw_simple_flag_row(war.defenders, war.original_defender, x + 427, y + 50, flag_overlay, is_attacker=False)

    def _draw_single_secondary_flag(self, x_pos, y_pos, tag, flag_overlay):
        """Draw a single secondary flag at the specified position."""
        # Use normal rectangular flag loading for secondary flags
        flag = self._load_country_flag_for_display(tag, circular=False)
        
        # CREATE FLAG CANVAS ITEM
        if flag:
            # Apply normal overlay if available
            if flag_overlay:
                flag = flag.copy()
                flag.alpha_composite(flag_overlay)
            
            photo = ImageTk.PhotoImage(flag)
            self.image_refs.append(photo)
            
            # Draw directly on main canvas
            flag_id = self.canvas.create_image(x_pos, y_pos, anchor=tk.NW, image=photo)
            self._tab3_widgets.append(flag_id)
        else:
            # Create a placeholder if flag image failed to load
            placeholder = Image.new("RGBA", (FLAG_WIDTH, FLAG_HEIGHT), (128, 128, 128, 255))
            draw = ImageDraw.Draw(placeholder)
            draw.rectangle([0, 0, FLAG_WIDTH, FLAG_HEIGHT], outline="black")
            text = tag if tag else "?"
            try:
                bbox = self.font_manager.font_8.getbbox(text)
                text_x = (FLAG_WIDTH - (bbox[2] - bbox[0])) // 2
                text_y = (FLAG_HEIGHT - (bbox[3] - bbox[1])) // 2
                draw.text((text_x, text_y), text, font=self.font_manager.font_8, fill="white")
            except:
                pass
            
            photo = ImageTk.PhotoImage(placeholder)
            self.image_refs.append(photo)
            flag_id = self.canvas.create_image(x_pos, y_pos, anchor=tk.NW, image=photo)
            self._tab3_widgets.append(flag_id)
        
        # ADD TOOLTIP AND HOVER FUNCTIONALITY TO SECONDARY FLAGS - MAKE SURE THIS EXISTS
        self.canvas.tag_bind(flag_id, "<Enter>", lambda e, t=tag: self._on_any_flag_enter(e, t))
        self.canvas.tag_bind(flag_id, "<Leave>", self._on_any_flag_leave)
        self.canvas.tag_bind(flag_id, "<Motion>", lambda e, t=tag: self._on_any_flag_motion(e, t))

    def _draw_simple_flag_row(self, tags: List[str], highlight: Optional[str], x: int, y: int, flag_overlay: Optional[Image.Image], is_attacker: bool):
        """Draw a row of flags with the first flag larger and secondary flags below in multiple rows."""
        tags = [t for t in tags if t and t != "---"]
        if not tags:
            return
        
        # Different rules for attackers vs defenders
        if is_attacker:
            first_row_max_width = 125  # Attackers: 125px for first row
            max_total_width = 124 + 174  # Attackers: 124 + 174 = 298px total
        else:
            first_row_max_width = 150  # Defenders: 150px for first row  
            max_total_width = 149 + 174  # Defenders: 149 + 174 = 323px total
        
        # Reorder to put highlighted tag first
        ordered = list(tags)
        if highlight in ordered:
            ordered.remove(highlight)
            ordered.insert(0, highlight)
        
        # ALWAYS DRAW FIRST FLAG (big flag)
        first_tag = ordered[0]
        flag_x = x
        flag_y = y

        # Load flag with appropriate overlay based on Great Power status
        flag = self._load_country_flag_for_display(first_tag, circular=True, gp_size=True)
        if flag:
            photo = ImageTk.PhotoImage(flag)
            self.image_refs.append(photo)
            
            flag_id = self.canvas.create_image(flag_x, flag_y, anchor=tk.NW, image=photo, tags=(f"country_{first_tag}",))
            self._tab3_widgets.append(flag_id)
        else:
            # Fallback without overlay
            placeholder = Image.new("RGBA", (52, 44), (128, 128, 128, 255))
            draw = ImageDraw.Draw(placeholder)
            draw.rectangle([0, 0, 52, 44], outline="black")
            text = first_tag if first_tag else "?"
            try:
                bbox = self.font_manager.font_8.getbbox(text)
                text_x = (52 - (bbox[2] - bbox[0])) // 2
                text_y = (44 - (bbox[3] - bbox[1])) // 2
                draw.text((text_x, text_y), text, font=self.font_manager.font_8, fill="white")
            except:
                pass
            
            photo = ImageTk.PhotoImage(placeholder)
            self.image_refs.append(photo)
            flag_id = self.canvas.create_image(flag_x + 6, flag_y + 8, anchor=tk.NW, image=photo, tags=(f"country_{first_tag}",))
            self._tab3_widgets.append(flag_id)

        # ADD COUNTRY NAME TEXT BELOW THE BIG FLAG
        if first_tag:
            # Get country name
            gov = self.state.country_governments.get(first_tag, "Unknown")
            country_name = LocalizationParser.get_country_name(self.state, first_tag, gov)
            
            # Truncate long names (more than 20 characters)
            if len(country_name) > 22:
                display_name = country_name[:20] + "..."
            else:
                display_name = country_name

            # Render text using BMFont
            name_img = self.font_manager.render_text(display_name, size=15, color="white")
            if name_img is not None:
                # Crop transparent padding for proper positioning
                bbox = name_img.getbbox()
                if bbox:
                    name_img = name_img.crop(bbox)
                
                name_photo = ImageTk.PhotoImage(name_img)
                self.image_refs.append(name_photo)
                
                # Position text below the flag
                name_x = flag_x + 55
                name_y = flag_y + 12
                name_id = self.canvas.create_image(name_x, name_y, anchor=tk.NW, image=name_photo)
                self._tab3_widgets.append(name_id)

        # Bind events directly to the flag image
        self.canvas.tag_bind(flag_id, "<Enter>", lambda e, t=first_tag: self._on_any_flag_enter(e, t))
        self.canvas.tag_bind(flag_id, "<Leave>", self._on_any_flag_leave)
        self.canvas.tag_bind(flag_id, "<Motion>", lambda e, t=first_tag: self._on_any_flag_motion(e, t))
        
        # Draw remaining flags BELOW the big flag in multiple rows (only if they exist)
        remaining_tags = ordered[1:]
        if remaining_tags:
            # Configuration for secondary flags
            row_height = 20
            min_spacing = 4  # Minimum spacing between flags
            max_spacing = 25  # Maximum spacing between flags
            
            # Calculate total space needed for all flags with maximum spacing
            total_flags = len(remaining_tags)
            total_width_needed_max = total_flags * max_spacing
            
            if total_width_needed_max <= max_total_width:
                # All flags fit with maximum spacing - use max spacing
                actual_spacing = max_spacing
            else:
                # Calculate the optimal spacing to fill the total available width
                actual_spacing = max_total_width / total_flags
                # Constrain within min/max bounds
                actual_spacing = max(min_spacing, min(max_spacing, actual_spacing))
            
            # Now distribute flags between the two rows with this spacing
            # First row can hold flags up to first_row_max_width
            max_flags_first_row = int(first_row_max_width // actual_spacing)
            
            first_row = remaining_tags[:max_flags_first_row]
            second_row = remaining_tags[max_flags_first_row:]
            
            # Calculate starting positions
            first_row_width = len(first_row) * actual_spacing
            first_row_start_x = x + 53  # Align with big flag
            
            second_row_width = len(second_row) * actual_spacing
            second_row_start_x = x + 3
            
            start_y = y + 35
            
            # Draw first row
            if first_row:
                for col_index, tag in enumerate(first_row):
                    flag_x_pos = first_row_start_x + col_index * actual_spacing
                    flag_y_pos = start_y
                    
                    self._draw_single_secondary_flag(flag_x_pos, flag_y_pos, tag, flag_overlay)
            
            # Draw second row
            if second_row:
                for col_index, tag in enumerate(second_row):
                    flag_x_pos = second_row_start_x + col_index * actual_spacing
                    flag_y_pos = start_y + row_height
                    
                    self._draw_single_secondary_flag(flag_x_pos, flag_y_pos, tag, flag_overlay)

    def emergency_cleanup(self):
        """Selective memory cleanup - preserve UI elements."""
        import gc
        
        # Debug: Show what's in caches before cleanup
        main_cache_size = len(self.state.image_cache.cache)
        war_cache_size = len(self.state.war_image_cache.cache)
        image_refs_size = len(self.image_refs)
        
        # ONLY clear war cache (flags, battle images) - preserve main cache (UI elements)
        war_cache_keys_before = list(self.state.war_image_cache.cache.keys())
        self.state.war_image_cache.clear()
        
        # Clear current tab's image references but preserve UI images
        ui_images_to_keep = []
        for ref in self.image_refs:
            # Keep images that are UI elements (backgrounds, buttons, etc.)
            # You might need to adjust this logic based on how you track UI images
            if any(ui_key in str(ref) for ui_key in ['layer_', 'bg', 'button']):
                ui_images_to_keep.append(ref)
        
        self.image_refs.clear()
        self.image_refs.extend(ui_images_to_keep)
        
        # Clear war detail widgets only
        if hasattr(self, "_tab3_widgets"):
            self._tab3_widgets.clear()
        
        # Force garbage collection
        gc.collect()
        
    def _load_country_flag_for_display(self, tag: str, circular: bool = False, gp_size: bool = False) -> Optional[Image.Image]:
        """Load flag with aggressive memory limits."""
        # Don't load flags if we're switching away from war details
        if len(self.image_refs) > 500:  # If we have too many images already
            return None
        
        cache_key = f"flag_{tag}_gp" if (circular and gp_size) else f"flag_{tag}_circular" if circular else f"flag_{tag}"
        
        # ALL FLAGS GO TO WAR CACHE (clearable)
        cached_flag = self.state.war_image_cache.get(cache_key)
        if cached_flag:
            if gp_size:
                return cached_flag
            return cached_flag.resize((FLAG_WIDTH, FLAG_HEIGHT), Image.Resampling.LANCZOS)
        
        # Your existing loading code...
        gov_name = self.state.country_governments.get(tag)
        if gov_name:
            flag_type = self.state.gov_to_flagtype.get(gov_name)
            if flag_type:
                variant_path = self.state.get_modded_path(os.path.join("gfx", "flags", f"{tag}_{flag_type}.tga"))
                flag = SafeLoader.safe_load_image(variant_path)
                if flag:
                    # Optimize large flags
                    if flag.width > 200 or flag.height > 150:
                        flag = flag.resize((100, 75), Image.Resampling.LANCZOS)
                    
                    if circular:
                        if gp_size:
                            flag = self._apply_circular_mask(flag, (40, 28))
                            if tag in self.state.great_nations:
                                flag = self._apply_gp_overlay(flag, (52, 44))
                            else:
                                flag = self._apply_secondary_overlay(flag, (46, 44))
                        else:
                            flag = self._apply_circular_mask(flag, (FLAG_WIDTH, FLAG_HEIGHT))
                    elif not gp_size:
                        flag = flag.resize((FLAG_WIDTH, FLAG_HEIGHT), Image.Resampling.LANCZOS)
                    
                    # STORE IN WAR CACHE
                    self.state.war_image_cache.set(cache_key, flag)
                    return flag
        
        # Fallback to base flag
        base_path = self.state.get_modded_path(os.path.join("gfx", "flags", f"{tag}.tga"))
        flag = SafeLoader.safe_load_image(base_path)
        if flag:
            # Optimize large flags
            if flag.width > 200 or flag.height > 150:
                flag = flag.resize((100, 75), Image.Resampling.LANCZOS)
            
            if circular:
                if gp_size:
                    flag = self._apply_circular_mask(flag, (40, 28))
                    if tag in self.state.great_nations:
                        flag = self._apply_gp_overlay(flag, (52, 44))
                    else:
                        flag = self._apply_secondary_overlay(flag, (46, 44))
                else:
                    flag = self._apply_circular_mask(flag, (FLAG_WIDTH, FLAG_HEIGHT))
            elif not gp_size:
                flag = flag.resize((FLAG_WIDTH, FLAG_HEIGHT), Image.Resampling.LANCZOS)
            
            # STORE IN WAR CACHE
            self.state.war_image_cache.set(cache_key, flag)
        
        return flag

    def _apply_gp_overlay(self, flag_img: Image.Image, overlay_size: Tuple[int, int] = (52, 44)) -> Image.Image:
        """Apply Great Power overlay frame using diplo_gp_overlay.dds."""
        # Load GP overlay frame
        overlay_path = self.state.get_modded_path(os.path.join("gfx", "interface", "diplo_gp_overlay.dds"))
        overlay = SafeLoader.safe_load_image(overlay_path, size=overlay_size)
        
        if not overlay:
            # Fallback: create a simple GP-style overlay
            overlay = Image.new("RGBA", overlay_size, (0, 0, 0, 0))
            draw = ImageDraw.Draw(overlay)
            # Draw a fancy GP-style frame
            draw.ellipse([0, 0, overlay_size[0], overlay_size[1]], outline="gold", width=3)
        
        # Create composite image with flag and overlay
        composite = Image.new("RGBA", overlay_size, (0, 0, 0, 0))
        
        # Center the flag within the overlay (flag is 40x28, overlay is 52x44)
        flag_x = (overlay_size[0] - 40) // 2
        flag_y = (overlay_size[1] - 28) // 2
        composite.paste(flag_img, (flag_x, flag_y))
        
        # Add the overlay frame
        composite.alpha_composite(overlay)
        
        return composite

    def _apply_secondary_overlay(self, flag_img: Image.Image, overlay_size: Tuple[int, int] = (46, 44)) -> Image.Image:
        """Apply secondary power overlay frame using diplo_2nd_overlay.dds with 3px right offset for both flag and overlay."""
        # Load secondary overlay frame
        overlay_path = self.state.get_modded_path(os.path.join("gfx", "interface", "diplo_2nd_overlay.dds"))
        overlay = SafeLoader.safe_load_image(overlay_path, size=overlay_size)
        
        if not overlay:
            # Fallback: create a simple secondary-style overlay
            overlay = Image.new("RGBA", overlay_size, (0, 0, 0, 0))
            draw = ImageDraw.Draw(overlay)
            # Draw a simple rectangular frame for secondary powers
            draw.rectangle([0, 0, overlay_size[0], overlay_size[1]], outline="silver", width=2)
        
        # Create a larger canvas to accommodate the 3px right offset
        # The final image will be 49px wide (46 + 3) to keep everything aligned
        canvas_width = overlay_size[0] + 3  # 46 + 3 = 49
        canvas_height = overlay_size[1]     # 44
        
        composite = Image.new("RGBA", (canvas_width, canvas_height), (0, 0, 0, 0))
        
        # Position both flag and overlay with 3px right offset
        flag_x = 3 + (overlay_size[0] - 40) // 2  # 3px offset + centered within overlay
        flag_y = (overlay_size[1] - 28) // 2
        
        overlay_x = 3  # Overlay also gets 3px right offset
        
        # Paste the flag
        composite.paste(flag_img, (flag_x, flag_y))
        
        # Add the overlay frame
        composite.alpha_composite(overlay, (overlay_x, 0))
        
        return composite

    def _apply_circular_mask(self, flag_img: Image.Image, mask_size: Tuple[int, int] = (40, 28)) -> Image.Image:
        """Apply circular mask to a flag image using diplo_gp_mask.tga."""
        # Load the circular mask (used for both GP and secondary)
        mask_path = self.state.get_modded_path(os.path.join("gfx", "interface", "diplo_gp_mask.tga"))
        mask = SafeLoader.safe_load_image(mask_path, size=mask_size)
        
        if not mask:
            # Fallback: create a simple circular mask
            mask = Image.new("L", mask_size, 0)
            draw = ImageDraw.Draw(mask)
            draw.ellipse([0, 0, mask_size[0], mask_size[1]], fill=255)
        else:
            # Extract the alpha channel for the mask
            if mask.mode == 'RGBA':
                mask = mask.getchannel('A')
            elif mask.mode == 'LA':
                mask = mask.getchannel('A') 
            else:
                mask = mask.convert('L')
        
        # Resize flag to match mask size
        flag_resized = flag_img.resize(mask_size, Image.Resampling.LANCZOS)
        
        # Ensure flag has alpha channel
        if flag_resized.mode != 'RGBA':
            flag_resized = flag_resized.convert('RGBA')
        
        # Apply the circular mask to the flag
        flag_resized.putalpha(mask)
        
        return flag_resized

    # ===== BATTLE LIST METHODS =====

    def _load_battle_row_background(self, path: str) -> Optional[Image.Image]:
        """Load and crop battle row background image - FIXED to crop from RIGHT side with dual cache."""
        # Battle backgrounds are war-specific and go in war cache
        cache_key = f"battle_bg_{os.path.basename(path)}"
        cached_bg = self.state.war_image_cache.get(cache_key)
        if cached_bg:
            return cached_bg
        
        full_img = SafeLoader.safe_load_image(path)
        if not full_img:
            return None
        
        crop_width = 620
        if full_img.width >= crop_width:
            # Crop from the RIGHT side, not the left
            row_bg = ImageLoader.crop(full_img, right=full_img.width - crop_width)
        else:
            row_bg = full_img
        
        final_bg = row_bg.resize((crop_width, 24), Image.Resampling.NEAREST)
        
        # Store in WAR cache (will be cleared on cleanup)
        self.state.war_image_cache.set(cache_key, final_bg)
        
        return final_bg

    def _draw_battle_row(self, parent, battle: Battle, index: int, row_bg: Optional[Image.Image]):
        """Draw a single battle row as a clickable item with COMPOSITE unit panels."""
        row = tk.Frame(parent, bg=BG_COLOR, height=24)
        row.pack(fill="x", pady=0)
        
        row_canvas = tk.Canvas(row, width=620, height=24, highlightthickness=0, bg=BG_COLOR)
        row_canvas.pack(side="left", anchor="w")
        
        # Background
        if row_bg:
            photo = ImageTk.PhotoImage(row_bg)
            self.image_refs.append(photo)
            row_canvas.create_image(0, 0, anchor="nw", image=photo)
        
        # Battle information
        battle_name = battle.name if len(battle.name) <= 25 else battle.name[:22] + "..."
        
        # Main battle info
        main_text = f"{index + 1}. {battle_name}"
        text_photo = self.text_renderer.render_cached_text(main_text, size=12, color="black", bold=True)
        row_canvas.create_image(10, 12, anchor="w", image=text_photo)
        
        # Get country tags and calculate total army sizes - FIXED: Use all units
        attacker_country_tag = battle.attacker.get('country', '?')
        defender_country_tag = battle.defender.get('country', '?')
        
        attacker_units = battle.attacker.get('units', {})
        defender_units = battle.defender.get('units', {})
        
        # Calculate total army sizes - FIXED: Sum ALL units
        attacker_army_size = sum(attacker_units.values())
        defender_army_size = sum(defender_units.values())
        
        attacker_losses = battle.attacker.get('losses', 0)
        defender_losses = battle.defender.get('losses', 0)
        
        # Format numbers to be compact (K for thousands)
        def format_number(num):
            if num >= 200000:
                return f"{num/1000000:.1f}M"
            elif num >= 1000000:
                return f"{num/1000000:.0f}M"
            elif num >= 10000:
                return f"{num/1000:.0f}K"
            elif num >= 1000:
                return f"{num/1000:.1f}K"
            else:
                return str(num)
        
        attacker_size_str = format_number(attacker_army_size)
        defender_size_str = format_number(defender_army_size)
        
        # Determine if this is a naval battle
        is_naval_battle = UnitTypeClassifier.is_naval_battle_for_panel(battle, self._unit_types)
        
        # Position for unit panels
        panels_start_x = 207
        panel_spacing = 10
        
        # Create and draw attacker unit panel
        if attacker_country_tag and attacker_country_tag != "?":
            attacker_panel = self._create_unit_panel(
                attacker_country_tag, 
                attacker_size_str,
                is_attacker=True,
                is_winner=(battle.result is True),
                is_naval=is_naval_battle,
                initial_army=attacker_army_size,
                casualties=attacker_losses
            )
            if attacker_panel:
                photo_attacker = ImageTk.PhotoImage(attacker_panel)
                self.image_refs.append(photo_attacker)
                row_canvas.create_image(panels_start_x, 1, anchor="nw", image=photo_attacker)
        
        # UPDATED: Load and display army_icon_2.dds cropped to 24x24
        army_icon_path = self.state.get_modded_path(os.path.join("gfx", "interface", "army_icon_2.dds"))
        army_icon = SafeLoader.safe_load_image(army_icon_path)
        
        if army_icon:
            # Crop 24px from the right to make it 24x24
            if army_icon.width >= 48 and army_icon.height >= 24:
                cropped_icon = army_icon.crop((0, 0, 24, 24))  # Left 24px of the 48x24 image
            else:
                # If image is wrong size, resize to 24x24
                cropped_icon = army_icon.resize((24, 24), Image.Resampling.LANCZOS)
            
            photo_army = ImageTk.PhotoImage(cropped_icon)
            self.image_refs.append(photo_army)
            # Position between the panels
            army_x = panels_start_x + 78 + 12 + 10  # Same position as before
            army_y = 12  # Center vertically
            row_canvas.create_image(army_x, army_y, anchor="center", image=photo_army)
        # else:
        #     # Fallback to "vs" text using BMFont if icon fails to load
        #     vs_text_img = self.font_manager.render_text("vs", size=8, color="black", bold=True)
        #     if vs_text_img:
        #         # Crop transparent padding
        #         bbox = vs_text_img.getbbox()
        #         if bbox:
        #             vs_text_img = vs_text_img.crop(bbox)
        #         vs_text_photo = ImageTk.PhotoImage(vs_text_img)
        #         self.image_refs.append(vs_text_photo)
        #         row_canvas.create_image(panels_start_x + 78 + 8, 12, anchor="center", image=vs_text_photo)
        
        # Create and draw defender unit panel
        defender_panel_x = panels_start_x + 78 + 24 + 20  # Account for army icon width
        if defender_country_tag and defender_country_tag != "?":
            defender_panel = self._create_unit_panel(
                defender_country_tag, 
                defender_size_str,
                is_attacker=False,
                is_winner=(battle.result is False),
                is_naval=is_naval_battle,
                initial_army=defender_army_size,
                casualties=defender_losses
            )
            if defender_panel:
                photo_defender = ImageTk.PhotoImage(defender_panel)
                self.image_refs.append(photo_defender)
                row_canvas.create_image(defender_panel_x, 1, anchor="nw", image=photo_defender)
        
        # Click handler for battle details popup
        def on_battle_click(event):
            self._show_battle_popup(battle)
        
        row_canvas.bind("<Button-1>", on_battle_click)
        row.bind("<Button-1>", on_battle_click)

    def _create_unit_panel(self, country_tag: str, army_size: str, 
                        is_attacker: bool, is_winner: bool, is_naval: bool,
                        initial_army: int, casualties: int) -> Optional[Image.Image]:
        """Create a composite unit panel with background, color overlay, flag, and army size text."""
        # Load Unit_Panel_bg.dds and crop according to battle type
        panel_bg_path = self.state.get_modded_path(os.path.join("gfx", "interface", "Unit_Panel_bg.dds"))
        panel_bg = SafeLoader.safe_load_image(panel_bg_path)
        
        if not panel_bg:
            return None
        
        # Crop Unit_Panel_bg.dds to 78x22
        # For land battle: start from 3px down and 3px left
        # For naval battle: start from 3px down and 87px from the left
        if is_naval:
            bg_crop_x = 87
        else:
            bg_crop_x = 3
        
        bg_crop_y = 3
        bg_crop_width = 78
        bg_crop_height = 22
        
        # Crop the background
        try:
            panel_bg_cropped = panel_bg.crop((bg_crop_x, bg_crop_y, bg_crop_x + bg_crop_width, bg_crop_y + bg_crop_height))
        except Exception as e:
            return None
        
        # Load unit_panel_color.dds and crop according to win/loss
        panel_color_path = self.state.get_modded_path(os.path.join("gfx", "interface", "unit_panel_color.dds"))
        panel_color = SafeLoader.safe_load_image(panel_color_path)
        
        if panel_color:
            # Crop unit_panel_color.dds to 76x20
            # For winning side: crop starts at 0x0
            # For losing side: crop starts at 76x0
            if is_winner:
                color_crop_x = 0
            else:
                color_crop_x = 76
            
            color_crop_y = 0
            color_crop_width = 76
            color_crop_height = 20
            
            try:
                panel_color_cropped = panel_color.crop((color_crop_x, color_crop_y, color_crop_x + color_crop_width, color_crop_y + color_crop_height))
            except Exception as e:
                panel_color_cropped = None
        else:
            panel_color_cropped = None
        
        # Load country flag with the SAME overlay as used in war details
        flag_overlay_path = self.state.get_modded_path(os.path.join("gfx", "interface", "flag_overlay_new.tga"))
        flag_overlay = SafeLoader.safe_load_image(flag_overlay_path, size=(FLAG_WIDTH, FLAG_HEIGHT))
        
        flag = self._load_country_flag_for_panel(country_tag)
        if flag:
            # Resize flag to standard flag size and apply the same overlay as war details
            flag_resized = flag.resize((FLAG_WIDTH, FLAG_HEIGHT), Image.Resampling.LANCZOS)
            
            # Apply the same flag overlay used in war details
            if flag_overlay:
                flag_resized = flag_resized.copy()
                flag_resized.alpha_composite(flag_overlay)
        else:
            flag_resized = None
        
        # Create composite image
        composite = Image.new("RGBA", (bg_crop_width, bg_crop_height), (0, 0, 0, 0))
        
        # 1. Add background
        composite.paste(panel_bg_cropped, (0, 0))
        
        # 2. Add color overlay with 1x1 pixel offset
        if panel_color_cropped:
            composite.alpha_composite(panel_color_cropped, (1, 1))
        
        # 3. Add flag with proper overlay - POSITION AT 2px from left as requested
        if flag_resized:
            flag_x = 2  # Position flag 2px from left edge
            flag_y = (bg_crop_height - FLAG_HEIGHT) // 2  # Keep vertical centering
            composite.alpha_composite(flag_resized, (flag_x, flag_y))
        
        # 4. ADD ARMY SIZE TEXT INSIDE THE PANEL, CENTERED AT THE SAME POSITION
        draw = ImageDraw.Draw(composite)
        
        # Use a small font that fits in the panel
        try:
            # Try to use a small system font
            font = ImageFont.truetype("arial.ttf", 10)
        except:
            try:
                font = ImageFont.truetype("Arial", 10)
            except:
                font = ImageFont.load_default()

        # Text color - white for better contrast
        text_color = (255, 255, 255)

        # Calculate text metrics for proper centering
        try:
            # Get the bounding box of the text
            bbox = font.getbbox(army_size)
            text_width = bbox[2] - bbox[0]
            text_height = bbox[3] - bbox[1]
        except:
            # Fallback if getbbox is not available
            text_width = len(army_size) * 5  # Rough estimate
            text_height = 8

        # Keep the same horizontal position (to the right of the flag)
        text_x = 2 + FLAG_WIDTH + 19  # Same as before: 2px left + 24px flag + 4px spacing

        # Center the text VERTICALLY at this position
        text_y = bg_crop_height // 2 - 1  # Middle of the panel

        # Draw army size text with MIDDLE-CENTER anchor
        draw.text((text_x, text_y), army_size, font=font, fill=text_color, anchor="mm")  # "mm" = middle, middle
        
        # 5. ADD CASUALTY BAR VISUALIZATION
        # Calculate survivor percentage using the passed parameters
        if initial_army > 0:
            survivor_percentage = max(0, min(1, (initial_army - casualties) / initial_army))
        else:
            # If initial army is 0, show empty bar (0% survivors)
            survivor_percentage = 0

        # Load and prepare the organization images
        org_bg_path = self.state.get_modded_path(os.path.join("gfx", "interface", "unitpanel_organisation_2.dds"))
        org_fg_path = self.state.get_modded_path(os.path.join("gfx", "interface", "unitpanel_organisation_1.dds"))

        org_bg = SafeLoader.safe_load_image(org_bg_path)
        org_fg = SafeLoader.safe_load_image(org_fg_path)

        if org_bg and org_fg:
            # Process background (red bar) - rotate 90° CW and flip vertically
            org_bg_processed = org_bg.rotate(-90, expand=True)  # -90 for clockwise
            org_bg_processed = org_bg_processed.transpose(Image.FLIP_TOP_BOTTOM)  # Flip vertically
            org_bg_processed = org_bg_processed.resize((6, 12), Image.Resampling.LANCZOS)  # Now 6x12
            
            # Process foreground (green bar) - rotate 90° CW and flip vertically
            org_fg_processed = org_fg.rotate(-90, expand=True)  # -90 for clockwise
            org_fg_processed = org_fg_processed.transpose(Image.FLIP_TOP_BOTTOM)  # Flip vertically
            org_fg_processed = org_fg_processed.resize((6, 12), Image.Resampling.LANCZOS)  # Now 6x12
            
            # Position for casualty bar (to the right of the text)
            bar_x = text_x + 23  # Your adjusted spacing
            bar_y = (bg_crop_height - 12) // 2  # Center vertically
            
            # Draw background (red) - ALWAYS draw the background, regardless of initial army
            composite.alpha_composite(org_bg_processed, (bar_x, bar_y))
            
            # Draw foreground (green) - height based on survivor percentage
            if survivor_percentage > 0:
                # Calculate how many pixels to show (from bottom up)
                fg_height = int(12 * survivor_percentage)
                if fg_height > 0:
                    # Crop the green bar to show only the bottom portion
                    fg_cropped = org_fg_processed.crop((0, 12 - fg_height, 6, 12))
                    composite.alpha_composite(fg_cropped, (bar_x, bar_y + (12 - fg_height)))
        
        return composite

    def _load_country_flag_for_panel(self, tag: str) -> Optional[Image.Image]:
        """Load flag image for display in unit panels (separate from war header method)."""
        if not tag or tag == "---" or tag == "?":
            return None
        
        cache_key = f"flag_{tag}_panel"
        cached_flag = self.state.image_cache.get(cache_key)
        if cached_flag:
            return cached_flag
        
        gov_name = self.state.country_governments.get(tag)
        if gov_name:
            flag_type = self.state.gov_to_flagtype.get(gov_name)
            if flag_type:
                variant_path = self.state.get_modded_path(os.path.join("gfx", "flags", f"{tag}_{flag_type}.tga"))
                flag = SafeLoader.safe_load_image(variant_path)
                if flag:
                    self.state.image_cache.set(cache_key, flag)
                    return flag
        
        base_path = self.state.get_modded_path(os.path.join("gfx", "flags", f"{tag}.tga"))
        flag = SafeLoader.safe_load_image(base_path)
        if flag:
            self.state.image_cache.set(cache_key, flag)
        return flag

# ============================================================================
# UPDATED BATTLE POPUP WITH DYNAMIC LEADERS
# ============================================================================

    def _show_battle_popup(self, battle: Battle):
        """Show enhanced battle popup with dynamic leader graphics."""
        # Load unit types first to handle modded units
        if not hasattr(self, '_unit_types'):
            self._unit_types = UnitTypeClassifier.load_unit_types(self.state)
        
        # Determine if this is a naval battle
        is_naval_battle = UnitTypeClassifier.is_naval_battle_for_panel(battle, self._unit_types)
        unit_analysis = UnitTypeClassifier.analyze_battle_units(battle, self._unit_types)
        
        popup = tk.Toplevel(self.root)
        popup.title(f"Battle: {battle.name}")
        popup.geometry("544x436")
        popup.resizable(False, False)
        popup.transient(self.root)
        popup.grab_set()
        
        # Center the popup
        popup.update_idletasks()
        x = self.root.winfo_x() + (self.root.winfo_width() - popup.winfo_width()) // 2
        y = self.root.winfo_y() + (self.root.winfo_height() - popup.winfo_height()) // 2
        popup.geometry(f"+{x}+{y}")
        
        canvas = tk.Canvas(popup, width=544, height=436, highlightthickness=0)
        canvas.pack()
        
        images_refs = []
        
        # Convert battle data to the format expected by the battle GUI
        #battle_info = self._convert_battle_to_info(battle)
        
        # Background - FIXED: Use proper naval background
        if is_naval_battle:
            bg_path = self.state.get_modded_path(os.path.join("gfx", "interface", "combat_end_bg.dds"))
        else:
            bg_path = self.state.get_modded_path(os.path.join("gfx", "interface", "combat_end_bg.dds"))
        
        bg_img = SafeLoader.safe_load_image(bg_path, (544, 436))
        if bg_img:
            bg_photo = ImageTk.PhotoImage(bg_img)
            canvas.create_image(0, 0, anchor=tk.NW, image=bg_photo)
            images_refs.append(bg_photo)
        
        # Terrain/background image based on battle type - UPDATED: Flip based on outcome
        if is_naval_battle:
            terrain_rel_path = os.path.join("gfx", "interface", "combat_end_naval_won.dds")
        else:
            terrain_rel_path = os.path.join("gfx", "interface", "combat_end_land_won.dds")
        
        terrain_path = self.state.get_modded_path(terrain_rel_path)
        terrain_img = SafeLoader.safe_load_image(terrain_path)
        if terrain_img:
            terrain_img = ImageLoader.crop(terrain_img, left=2, top=2, right=2, bottom=2)
            terrain_img = terrain_img.resize((507, 72), Image.Resampling.LANCZOS)
            
            # FLIP HORIZONTALLY:
            # - Naval battles: flip if attacker won
            # - Land battles: flip if defender won
            if (is_naval_battle and battle.result is True) or (not is_naval_battle and battle.result is False):
                terrain_img = terrain_img.transpose(Image.FLIP_LEFT_RIGHT)
            
            terrain_photo = ImageTk.PhotoImage(terrain_img)
            canvas.create_image(18, 46, anchor=tk.NW, image=terrain_photo)
            images_refs.append(terrain_photo)
        
        # Flags with overlay
        overlay_path = self.state.get_modded_path(os.path.join("gfx", "interface", "shield_frame.dds"))
        
        # Get country tags for flags
        attacker_country = battle.attacker.get('country', 'UNK')
        defender_country = battle.defender.get('country', 'UNK')
        
        # Load flags
        left_flag_img = self._load_battle_flag(attacker_country, overlay_path, -90)
        right_flag_img = self._load_battle_flag(defender_country, overlay_path, -90)
        
        if left_flag_img:
            left_flag_photo = ImageTk.PhotoImage(left_flag_img)
            canvas.create_image(46, 82, anchor=tk.CENTER, image=left_flag_photo)
            images_refs.append(left_flag_photo)
        
        if right_flag_img:
            right_flag_photo = ImageTk.PhotoImage(right_flag_img)
            canvas.create_image(496, 82, anchor=tk.CENTER, image=right_flag_photo)
            images_refs.append(right_flag_photo)
        
        # DYNAMIC LEADERS - based on culture and battle type
        leader_size = (32, 40)
        
        # Get leader names
        attacker_leader_name = battle.attacker.get('leader', 'Unknown Commander')
        defender_leader_name = battle.defender.get('leader', 'Unknown Commander')
        
        # Check if we need to use no_leader.dds for attacker
        if attacker_leader_name in ["No Commander", "Unknown Commander", "Unknown"]:
            attacker_leader_path = self.state.get_modded_path(os.path.join("gfx", "interface", "leaders", "no_leader.dds"))
        else:
            attacker_leader_path = CultureLeaderMapper.get_leader_graphics(
                self.state, attacker_country, is_naval_battle
            )
            if not attacker_leader_path:
                # Fallback to default
                if is_naval_battle:
                    attacker_leader_path = self.state.get_modded_path(os.path.join("gfx", "interface", "leaders", "european_admiral_24.dds"))
                else:
                    attacker_leader_path = self.state.get_modded_path(os.path.join("gfx", "interface", "leaders", "european_general_24.dds"))
        
        # Check if we need to use no_leader.dds for defender
        if defender_leader_name in ["No Commander", "Unknown Commander", "Unknown"]:
            defender_leader_path = self.state.get_modded_path(os.path.join("gfx", "interface", "leaders", "no_leader.dds"))
        else:
            defender_leader_path = CultureLeaderMapper.get_leader_graphics(
                self.state, defender_country, is_naval_battle
            )
            if not defender_leader_path:
                # Fallback to default
                if is_naval_battle:
                    defender_leader_path = self.state.get_modded_path(os.path.join("gfx", "interface", "leaders", "european_admiral_2.dds"))
                else:
                    defender_leader_path = self.state.get_modded_path(os.path.join("gfx", "interface", "leaders", "european_general_2.dds"))
        
        attacker_leader = SafeLoader.safe_load_image(attacker_leader_path, leader_size)
        defender_leader = SafeLoader.safe_load_image(defender_leader_path, leader_size)
        
        if attacker_leader:
            attacker_leader_photo = ImageTk.PhotoImage(attacker_leader)
            canvas.create_image(46 - 11 - leader_size[0] // 2, 124, anchor=tk.NW, image=attacker_leader_photo)
            images_refs.append(attacker_leader_photo)
        
        if defender_leader:
            defender_leader_photo = ImageTk.PhotoImage(defender_leader)
            canvas.create_image(496 + 12 - leader_size[0] // 2, 124, anchor=tk.NW, image=defender_leader_photo)
            images_refs.append(defender_leader_photo)
        
        # Leader names - remove first name only if too long, then truncate if still too long
        def shorten_name(full_name):
            if len(full_name) > 20:
                # Try to remove first name (everything before first space)
                parts = full_name.split(' ', 1)
                if len(parts) > 1:
                    last_name = parts[1]
                    # Truncate last name if it's still too long
                    return last_name[:18] + "..." if len(last_name) > 20 else last_name
                else:
                    # If no space, just truncate
                    return full_name[:18] + "..."
            else:
                return full_name
        
        attacker_leader_display = shorten_name(attacker_leader_name)
        defender_leader_display = shorten_name(defender_leader_name)
        
        attacker_leader_text = self.font_manager.render_text(attacker_leader_display, size=18, color="black")
        # Crop transparent padding for proper anchor positioning
        bbox = attacker_leader_text.getbbox()
        if bbox:
            attacker_leader_text = attacker_leader_text.crop(bbox)
        attacker_leader_photo = ImageTk.PhotoImage(attacker_leader_text)
        canvas.create_image(55, 140, anchor=tk.NW, image=attacker_leader_photo)
        images_refs.append(attacker_leader_photo)

        defender_leader_text = self.font_manager.render_text(defender_leader_display, size=18, color="black")
        # Crop transparent padding for proper anchor positioning  
        bbox = defender_leader_text.getbbox()
        if bbox:
            defender_leader_text = defender_leader_text.crop(bbox)
        defender_leader_photo = ImageTk.PhotoImage(defender_leader_text)
        canvas.create_image(485, 140, anchor=tk.NE, image=defender_leader_photo)
        images_refs.append(defender_leader_photo)
                
        # Battle title
        title_text = self.font_manager.render_bold_text(f"Battle of {battle.name}", size=22, color="white")
        title_photo = ImageTk.PhotoImage(title_text)
        canvas.create_image(272, 42, anchor=tk.CENTER, image=title_photo)
        images_refs.append(title_photo)
        
        # Result text - direct image approach
        result_text = "Attacker Won" if battle.result is True else "Defender Won" if battle.result is False else "Indecisive"
        result_text_img = self.font_manager.render_text(result_text, size=32, color=(0, 159, 0))
        
        if result_text_img is not None:
            result_photo = ImageTk.PhotoImage(result_text_img)
            canvas.create_image(272, 352, anchor=tk.CENTER, image=result_photo)
            images_refs.append(result_photo)
        
        # Unit Table - DIFFERENT FOR NAVAL BATTLES
        if is_naval_battle:
            # NAVAL BATTLE: Show ship types
            self._draw_naval_unit_table(canvas, battle, unit_analysis, images_refs)
        else:
            # LAND BATTLE: Show land unit types
            self._draw_land_unit_table(canvas, battle, unit_analysis, images_refs)
        
        # Store image references to prevent garbage collection
        popup._images = images_refs

    def _draw_naval_unit_table(self, canvas, battle: Battle, unit_analysis: Dict, images_refs: list):
        """Draw naval unit table - Show initial ship types, but only totals for sunk/survivors."""
        unit_strip_path = self.state.get_modded_path(os.path.join("gfx", "interface", "unit_strip.dds"))
        unit_strip = SafeLoader.safe_load_image(unit_strip_path)
        
        if unit_strip:
            # Naval unit icon mapping
            icon_map = {
                'big_ship': 3,       # Big ships (battleships, dreadnoughts)
                'light_ship': 4,     # Light ships (cruisers, frigates)
                'transport': 5,      # Transports
            }
            
            # Naval unit order
            naval_unit_order = ['big_ship', 'light_ship', 'transport']
            
            base_x = 21
            icon_x = base_x
            atk_xs = [icon_x + 106, icon_x + 173, icon_x + 240]
            def_xs = [base_x + 394, base_x + 328, base_x + 262]
            start_y = 185
            row_spacing = 31
            
            # Get actual losses from battle data
            attacker_total_losses = battle.attacker.get('losses', 0)
            defender_total_losses = battle.defender.get('losses', 0)
            
            # Calculate total ships and survivors
            attacker_total_ships = sum(unit_analysis['attacker'].values())
            defender_total_ships = sum(unit_analysis['defender'].values())
            
            attacker_survivors = max(0, attacker_total_ships - attacker_total_losses)
            defender_survivors = max(0, defender_total_ships - defender_total_losses)
            
            # FIXED: Column headers with consistent positioning
            column_headers = ["Initial", "Casualties", "Survivors"]
            
            # Attacker column headers (right-aligned)
            for i, (label, x) in enumerate(zip(column_headers, atk_xs)):
                if label:
                    text_img = self.font_manager.render_text(label, size=12, color="black")
                    if text_img is not None:
                        # Crop transparent padding for proper centering
                        bbox = text_img.getbbox()
                        if bbox:
                            text_img = text_img.crop(bbox)
                        
                        photo = ImageTk.PhotoImage(text_img)
                        # Use CENTER anchor for consistent positioning regardless of text width
                        canvas.create_image(x - 30, start_y - 7, anchor=tk.CENTER, image=photo)
                        images_refs.append(photo)
            
            # Defender column headers (left-aligned)  
            for i, (label, x) in enumerate(zip(column_headers, def_xs)):
                if label:
                    text_img = self.font_manager.render_text(label, size=12, color="black")
                    if text_img is not None:
                        # Crop transparent padding for proper centering
                        bbox = text_img.getbbox()
                        if bbox:
                            text_img = text_img.crop(bbox)
                        
                        photo = ImageTk.PhotoImage(text_img)
                        # Use CENTER anchor for consistent positioning
                        canvas.create_image(x + 29, start_y - 7, anchor=tk.CENTER, image=photo)
                        images_refs.append(photo)
            
            # Ship type rows - ONLY SHOW INITIAL COUNTS
            for i, ship_type in enumerate(naval_unit_order):
                y = start_y + i * row_spacing
                
                # Get categorized ship counts from the analyzer
                atk_initial = unit_analysis['attacker'].get(ship_type, 0)
                def_initial = unit_analysis['defender'].get(ship_type, 0)
                
                # Draw ship icons
                if ship_type in icon_map:
                    icon = get_unit_icon(unit_strip, icon_map[ship_type])
                    icon_photo = ImageTk.PhotoImage(icon)
                    # Attacker icon (left)
                    canvas.create_image(icon_x, y, anchor=tk.NW, image=icon_photo)
                    # Defender icon (right) 
                    canvas.create_image(base_x + 464, y, anchor=tk.NW, image=icon_photo)
                    images_refs.append(icon_photo)
                
                # Attacker stats - ONLY INITIAL (always show first column, skip others)
                text_img = self.font_manager.render_text(str(atk_initial), size=18, color="black")
                if text_img is not None:
                    # ADD THIS: Crop transparent padding for proper anchor positioning
                    bbox = text_img.getbbox()
                    if bbox:
                        text_img = text_img.crop(bbox)
                    
                    photo = ImageTk.PhotoImage(text_img)
                    canvas.create_image(atk_xs[0] - 5, y + 18, anchor=tk.E, image=photo)
                    images_refs.append(photo)

                # Defender stats - ONLY INITIAL (always show first column, skip others)
                text_img = self.font_manager.render_text(str(def_initial), size=18, color="black")
                if text_img is not None:
                    # ADD THIS: Crop transparent padding for proper anchor positioning
                    bbox = text_img.getbbox()
                    if bbox:
                        text_img = text_img.crop(bbox)
                    
                    photo = ImageTk.PhotoImage(text_img)
                    canvas.create_image(def_xs[0] + 5, y + 18, anchor=tk.W, image=photo)
                    images_refs.append(photo)
            
            # Total row - SHOW ALL TOTALS (Initial, Sunk, Survivors)
            total_y = start_y + len(naval_unit_order) * row_spacing + 8
            
            # Attacker totals
            atk_total_vals = [
                attacker_total_ships,
                f"-{attacker_total_losses}",
                attacker_survivors
            ]
            for val, x in zip(atk_total_vals, atk_xs):
                text_img = self.font_manager.render_text(str(val), size=22, color="black")
                if text_img is not None:
                    # ADD THIS: Crop transparent padding for proper anchor positioning
                    bbox = text_img.getbbox()
                    if bbox:
                        text_img = text_img.crop(bbox)
                    
                    photo = ImageTk.PhotoImage(text_img)
                    canvas.create_image(x - 3, total_y + 15, anchor=tk.E, image=photo)
                    images_refs.append(photo)
            
            # Defender totals
            def_total_vals = [
                defender_total_ships,
                f"-{defender_total_losses}",
                defender_survivors
            ]
            for val, x in zip(def_total_vals, def_xs):
                text_img = self.font_manager.render_text(str(val), size=22, color="black")
                if text_img is not None:
                    # ADD THIS: Crop transparent padding for proper anchor positioning
                    bbox = text_img.getbbox()
                    if bbox:
                        text_img = text_img.crop(bbox)
                    
                    photo = ImageTk.PhotoImage(text_img)
                    canvas.create_image(x + 3, total_y + 15, anchor=tk.W, image=photo)
                    images_refs.append(photo)

    def _draw_land_unit_table(self, canvas, battle: Battle, unit_analysis: Dict, images_refs: list):
        """Draw land unit table - Show initial unit types, but only totals for casualties/survivors."""
        unit_strip_path = self.state.get_modded_path(os.path.join("gfx", "interface", "unit_strip.dds"))
        unit_strip = SafeLoader.safe_load_image(unit_strip_path)
        
        if unit_strip:
            # Land unit icon mapping - CAVALRY FIRST
            icon_map = {
                'cavalry': 1,       # Cavalry icon - FIRST ROW
                'infantry': 0,      # Infantry icon - SECOND ROW
                'support_special': 2,  # Artillery icon for support/special - THIRD ROW
            }
            
            # REORDERED: Cavalry first, then infantry, then support/special
            land_unit_order = ['cavalry', 'infantry', 'support_special']
            
            base_x = 21
            icon_x = base_x
            atk_xs = [icon_x + 106, icon_x + 173, icon_x + 240]
            def_xs = [base_x + 394, base_x + 328, base_x + 262]
            start_y = 185
            row_spacing = 31
            
            # Get actual losses from battle data
            attacker_total_losses = battle.attacker.get('losses', 0)
            defender_total_losses = battle.defender.get('losses', 0)
            
            # Calculate total army and survivors
            attacker_total_army = sum(unit_analysis['attacker'].values())
            defender_total_army = sum(unit_analysis['defender'].values())
            
            attacker_survivors = max(0, attacker_total_army - attacker_total_losses)
            defender_survivors = max(0, defender_total_army - defender_total_losses)
            
            # FIXED: Column headers with consistent positioning
            column_headers = ["Initial", "Casualties", "Survivors"]
            
            # Attacker column headers (right-aligned)
            for i, (label, x) in enumerate(zip(column_headers, atk_xs)):
                if label:
                    text_img = self.font_manager.render_text(label, size=12, color="black")
                    if text_img is not None:
                        # Crop transparent padding for proper centering
                        bbox = text_img.getbbox()
                        if bbox:
                            text_img = text_img.crop(bbox)
                        
                        photo = ImageTk.PhotoImage(text_img)
                        # Use CENTER anchor for consistent positioning regardless of text width
                        canvas.create_image(x - 30, start_y - 7, anchor=tk.CENTER, image=photo)
                        images_refs.append(photo)
            
            # Defender column headers (left-aligned)  
            for i, (label, x) in enumerate(zip(column_headers, def_xs)):
                if label:
                    text_img = self.font_manager.render_text(label, size=12, color="black")
                    if text_img is not None:
                        # Crop transparent padding for proper centering
                        bbox = text_img.getbbox()
                        if bbox:
                            text_img = text_img.crop(bbox)
                        
                        photo = ImageTk.PhotoImage(text_img)
                        # Use CENTER anchor for consistent positioning
                        canvas.create_image(x + 29, start_y - 7, anchor=tk.CENTER, image=photo)
                        images_refs.append(photo)
            
            # Land unit rows - ONLY SHOW INITIAL COUNTS
            for i, unit_type in enumerate(land_unit_order):
                y = start_y + i * row_spacing
                
                # Get categorized unit counts from the analyzer
                atk_initial = unit_analysis['attacker'].get(unit_type, 0)
                def_initial = unit_analysis['defender'].get(unit_type, 0)
                
                # Draw unit icons
                if unit_type in icon_map:
                    icon = get_unit_icon(unit_strip, icon_map[unit_type])
                    icon_photo = ImageTk.PhotoImage(icon)
                    # Attacker icon (left)
                    canvas.create_image(icon_x, y, anchor=tk.NW, image=icon_photo)
                    # Defender icon (right) 
                    canvas.create_image(base_x + 464, y, anchor=tk.NW, image=icon_photo)
                    images_refs.append(icon_photo)
                
                # Attacker stats - ONLY INITIAL
                text_img = self.font_manager.render_text(str(atk_initial), size=18, color="black")
                if text_img is not None:
                    # Crop transparent padding for proper anchor positioning
                    bbox = text_img.getbbox()
                    if bbox:
                        text_img = text_img.crop(bbox)
                    
                    photo = ImageTk.PhotoImage(text_img)
                    canvas.create_image(atk_xs[0] - 5, y + 18, anchor=tk.E, image=photo)
                    images_refs.append(photo)

                # Defender stats - ONLY INITIAL
                text_img = self.font_manager.render_text(str(def_initial), size=18, color="black")
                if text_img is not None:
                    # Crop transparent padding for proper anchor positioning
                    bbox = text_img.getbbox()
                    if bbox:
                        text_img = text_img.crop(bbox)
                    
                    photo = ImageTk.PhotoImage(text_img)
                    canvas.create_image(def_xs[0] + 5, y + 18, anchor=tk.W, image=photo)
                    images_refs.append(photo)
            
            # Total row - SHOW ALL TOTALS
            total_y = start_y + len(land_unit_order) * row_spacing + 8
            
            # Attacker totals
            atk_total_vals = [
                attacker_total_army,
                f"-{attacker_total_losses}",
                attacker_survivors
            ]
            for val, x in zip(atk_total_vals, atk_xs):
                text_img = self.font_manager.render_text(str(val), size=22, color="black")
                if text_img is not None:
                    # Crop transparent padding for proper anchor positioning
                    bbox = text_img.getbbox()
                    if bbox:
                        text_img = text_img.crop(bbox)
                    
                    photo = ImageTk.PhotoImage(text_img)
                    canvas.create_image(x - 3, total_y + 15, anchor=tk.E, image=photo)
                    images_refs.append(photo)
            
            # Defender totals
            def_total_vals = [
                defender_total_army,
                f"-{defender_total_losses}",
                defender_survivors
            ]
            for val, x in zip(def_total_vals, def_xs):
                text_img = self.font_manager.render_text(str(val), size=22, color="black")
                if text_img is not None:
                    # Crop transparent padding for proper anchor positioning
                    bbox = text_img.getbbox()
                    if bbox:
                        text_img = text_img.crop(bbox)
                    
                    photo = ImageTk.PhotoImage(text_img)
                    canvas.create_image(x + 3, total_y + 15, anchor=tk.W, image=photo)
                    images_refs.append(photo)

    def _load_battle_flag(self, country_tag: str, overlay_path: str, rotate_degrees: int = 0) -> Optional[Image.Image]:
        """Load a HIGH-RESOLUTION flag with overlay for battle popup."""
        target_size = (66, 51)
        
        # Load the flag at HIGH resolution directly from file
        flag = self._load_high_res_flag(country_tag, target_size)
        if not flag:
            return None
        
        # Load overlay
        overlay = SafeLoader.safe_load_image(overlay_path, target_size)
        if not overlay:
            return flag
        
        # Apply rotation if needed
        if rotate_degrees:
            flag = flag.rotate(rotate_degrees, expand=True)
            overlay = overlay.rotate(rotate_degrees, expand=True)
        
        # Composite overlay onto flag
        flag = flag.copy()
        flag.alpha_composite(overlay)
        
        return flag

    def _load_high_res_flag(self, tag: str, target_size: Tuple[int, int] = (66, 51)) -> Optional[Image.Image]:
        """Load flag at original resolution and resize once for maximum quality."""
        if not tag or tag == "---":
            return None
        
        cache_key = f"flag_hr_{tag}_{target_size[0]}x{target_size[1]}"
        cached_flag = self.state.image_cache.get(cache_key)
        if cached_flag:
            return cached_flag
        
        # Load flag WITHOUT any initial resize
        gov_name = self.state.country_governments.get(tag)
        if gov_name:
            flag_type = self.state.gov_to_flagtype.get(gov_name)
            if flag_type:
                variant_path = self.state.get_modded_path(os.path.join("gfx", "flags", f"{tag}_{flag_type}.tga"))
                flag = SafeLoader.safe_load_image(variant_path)
                if flag:
                    # Single high-quality resize to target
                    flag = flag.resize(target_size, Image.Resampling.LANCZOS)
                    self.state.image_cache.set(cache_key, flag)
                    return flag
        
        base_path = self.state.get_modded_path(os.path.join("gfx", "flags", f"{tag}.tga"))
        flag = SafeLoader.safe_load_image(base_path)
        if flag:
            # Single high-quality resize to target
            flag = flag.resize(target_size, Image.Resampling.LANCZOS)
            self.state.image_cache.set(cache_key, flag)
        
        return flag
    
    def _convert_battle_to_info(self, battle: Battle) -> Dict:
        """Convert Battle dataclass to the format expected by battle GUI."""
        return {
            "name": battle.name,
            "result": "Attacker Victory" if battle.result is True else "Defender Victory" if battle.result is False else "Indecisive",
            "attacker_flag": f"{battle.attacker.get('country', 'UNK')}.tga",
            "defender_flag": f"{battle.defender.get('country', 'UNK')}.tga",
            "attacker_leader": "european_general_24.dds",
            "defender_leader": "european_general_2.dds",
            "attacker_leader_name": battle.attacker.get('leader', 'Unknown Commander'),
            "defender_leader_name": battle.defender.get('leader', 'Unknown Commander'),
            "terrain_image": "terrain_plains.tga",  # Default for now
            "attacker_units": battle.attacker.get('units', {}),
            "defender_units": battle.defender.get('units', {}),
        }

    # ===== WAR GOAL TRANSLATION METHODS =====

    def _translate_war_goal(self, war_goal: WarGoal, war: War) -> str:
        """Translate war goal using the stored WarGoal object."""
        # Make sure we have localization data loaded
        if not hasattr(self.state, 'localization_names') or not self.state.localization_names:
            self.state.localization_names = LocalizationParser.parse_localization_files(self.state)
        
        # Translate CB name
        translated_cb = None
        cb_name = war_goal.casus_belli
        if cb_name:
            translated_cb = self.state.localization_names.get(cb_name)
            if not translated_cb:
                # Try common variations
                variations = [
                    cb_name,
                    cb_name.lower(),
                    cb_name.upper(),
                    f"_{cb_name}",
                    f"_{cb_name.lower()}",
                ]
                for variation in variations:
                    if variation in self.state.localization_names:
                        translated_cb = self.state.localization_names[variation]
                        break
            if not translated_cb:
                translated_cb = cb_name
        
        # Use the war goal's actor and receiver (which now have war leader fallback)
        goal_actor = war_goal.actor
        goal_receiver = war_goal.receiver
        
        # Get country names - these should now always be valid
        actor_gov = self.state.country_governments.get(goal_actor, "Unknown")
        receiver_gov = self.state.country_governments.get(goal_receiver, "Unknown")
        
        actor_name = LocalizationParser.get_country_name(self.state, goal_actor, actor_gov)
        receiver_name = LocalizationParser.get_country_name(self.state, goal_receiver, receiver_gov)
        
        # Translate state name if available
        state_name = None
        if war_goal.state_province_id:
            state_name = self._get_state_name(war_goal.state_province_id)
        
        # Construct the translated goal
        cb_display = (translated_cb or cb_name).lower()
        
        if war_goal.state_province_id and state_name:
            translated_goal = f"{actor_name} {cb_display} of {state_name} against {receiver_name}"
        elif war_goal.state_province_id:
            translated_goal = f"{actor_name} {cb_display} of State {war_goal.state_province_id} against {receiver_name}"
        else:
            translated_goal = f"{actor_name} {cb_display} against {receiver_name}"
        
        return translated_goal

    def _get_state_name(self, state_id: str) -> Optional[str]:
        """Get the name of a state by its ID from localization files."""
        if not hasattr(self.state, 'localization_names') or not self.state.localization_names:
            return None
        
        # Victoria 2 province names are stored as PROV{id} in localization
        target_key = f"PROV{state_id}"
        
        # Check exact matches first
        for key in [target_key, target_key.upper(), target_key.lower()]:
            if key in self.state.localization_names:
                return self.state.localization_names[key]
        
        # Look for any key that contains the province ID (case insensitive)
        for key, value in self.state.localization_names.items():
            if state_id in key and 'PROV' in key.upper():
                return value

        return None

    def _draw_war_score(self, canvas, war_score: float, x: int, y: int):
        """Draw war score visualization for both war list and detailed view."""
        # War score is already -100 to +100, no normalization needed
        
        # Load overlay and progress bars at original size
        overlay_path = self.state.get_modded_path(os.path.join("gfx", "interface", "diplo_war_progress_overlay.dds"))
        progress1_path = self.state.get_modded_path(os.path.join("gfx", "interface", "diplo_war_progress1.dds"))
        progress2_path = self.state.get_modded_path(os.path.join("gfx", "interface", "diplo_war_progress2.dds"))
        
        overlay = SafeLoader.safe_load_image(overlay_path, size=(224, 24))
        progress1 = SafeLoader.safe_load_image(progress1_path, size=(216, 16))
        progress2 = SafeLoader.safe_load_image(progress2_path, size=(216, 16))
        
        if overlay and progress1 and progress2:
            # Create composite background image
            composite = Image.new("RGBA", (224, 24), (0, 0, 0, 0))
            
            # Position progress bars
            progress_x = 4
            progress_y = 4
            
            # ALWAYS draw defender progress bar (progress2) at full width
            composite.paste(progress2, (progress_x, progress_y))
            
            # Convert war score (-100 to +100) to percentage of progress1 to show (0% to 1.0)
            progress1_percentage = (war_score + 100) / 200.0
            
            # Calculate crop amount: 0% progress1 = full crop (216px), 100% progress1 = no crop (0px)
            crop_amount = int(216 * (1.0 - progress1_percentage))
            crop_amount = max(0, min(216, crop_amount))
            
            if crop_amount < 216:
                progress1_cropped = progress1.crop((0, 0, 216 - crop_amount, 16))
                composite.paste(progress1_cropped, (progress_x, progress_y))
            
            # Add the overlay on top
            composite.alpha_composite(overlay)
            
            # Convert background to PhotoImage and add to canvas
            bg_photo = ImageTk.PhotoImage(composite)
            self.image_refs.append(bg_photo)
            #self.current_page_images.append(bg_photo)  # TRACK FOR CLEANUP
            canvas.create_image(x, y, anchor=tk.CENTER, image=bg_photo)
            
            # Add percentage text with BMFont (separate layer)
            if war_score > 0:
                percentage_text = f"{int(war_score)}%"
            elif war_score < 0:
                percentage_text = f"{int(war_score)}%"
            else:
                percentage_text = "0%"
            
            # Render text using BMFont
            text_image = self.font_manager.render_text(percentage_text, size=18, color="black")
            
            if text_image is not None:
                # Crop transparent padding for proper centering
                bbox = text_image.getbbox()
                if bbox:
                    text_image = text_image.crop(bbox)
                
                # Convert text to PhotoImage
                text_photo = ImageTk.PhotoImage(text_image)
                self.image_refs.append(text_photo)
                #self.current_page_images.append(text_photo)  # TRACK FOR CLEANUP
                
                # Add text as separate layer above background (same position)
                canvas.create_image(x, y, anchor=tk.CENTER, image=text_photo)

    def _draw_war_details_tab(self):
        """Draw the war details tab with memory tracking."""
        # Track how many images we create in this tab
        start_image_count = len(self.image_refs)
        if hasattr(self, "_tab3_widgets"):
            for wid in self._tab3_widgets:
                try:
                    self.canvas.delete(wid)
                except Exception:
                    pass
            self._tab3_widgets.clear()
        else:
            self._tab3_widgets = []

        detail_x, detail_y = 30, 80

        # Guard against invalid selection
        if (self.state.selected_war_index is None or 
            self.state.selected_war_index >= len(self.state.filtered_wars)):
            # Render text using BMFont
            text_img = self.font_manager.render_text("No war selected. Select one in 'Show wars' tab.", size=12, color="black")
            if text_img is not None:
                text_photo = ImageTk.PhotoImage(text_img)
                self.image_refs.append(text_photo)
                wid = self.canvas.create_image(detail_x, detail_y, anchor=tk.NW, image=text_photo)
                self._tab3_widgets.append(wid)
            return

        war = self.state.filtered_wars[self.state.selected_war_index]
        
        # PRE-CACHE ALL TOOLTIP TEXTS FOR THIS WAR
        self._cache_flag_tooltips(war)
        
        # Add crisis background with correct size 638x249
        crisis_bg_path = self.state.get_modded_path(os.path.join("gfx", "interface", "diplo_crises_bg.dds"))
        crisis_bg = SafeLoader.safe_load_image(crisis_bg_path, size=(638, 249))
        
        if crisis_bg:
            photo_bg = ImageTk.PhotoImage(crisis_bg)
            self.image_refs.append(photo_bg)
            bg_id = self.canvas.create_image(detail_x - 6, detail_y - 28, anchor=tk.NW, image=photo_bg)
            self._tab3_widgets.append(bg_id)
        
        # War header with flags
        self._draw_war_header_simple(war, detail_x - 6, detail_y)
        
        # ===== STANDALONE DATE DISPLAY =====
        # Format dates for display
        start_date_str = war.start_date if war.start_date else "Unknown"
        end_date_str = war.end_date if war.end_date else "Ongoing"
        date_info = f"Dates: {start_date_str} to {end_date_str}"
        
        # Render date using BMFont
        date_img = self.font_manager.render_text(date_info, size=15, color="black")
        if date_img is not None:
            date_photo = ImageTk.PhotoImage(date_img)
            self.image_refs.append(date_photo)
            date_y_position = detail_y + 50
            date_id = self.canvas.create_image(detail_x + 312, date_y_position, anchor=tk.N, image=date_photo)
            self._tab3_widgets.append(date_id)

        # ===== MILITARY STATISTICS GRAPHICS =====
        stats_y_position = detail_y + 86  # Position below the date

        # Calculate military statistics
        land_stats = self._calculate_land_statistics(war)
        naval_stats = self._calculate_naval_statistics(war)

        # Load icons
        army_icon_path = self.state.get_modded_path(os.path.join("gfx", "interface", "topbar_army.dds"))
        navy_icon_path = self.state.get_modded_path(os.path.join("gfx", "interface", "topbar_navy.dds"))
        casualties_icon_path = self.state.get_modded_path(os.path.join("gfx", "interface", "diplo_we_icon.dds"))

        army_icon = SafeLoader.safe_load_image(army_icon_path, size=(32, 32))
        navy_icon = SafeLoader.safe_load_image(navy_icon_path, size=(32, 32))
        casualties_icon = SafeLoader.safe_load_image(casualties_icon_path, size=(24, 28))

        # Land battles summary
        land_battles_count = len(land_stats['land_battles'])
        attacker_land_wins_count = land_stats['attacker_land_wins']
        defender_land_wins_count = land_stats['defender_land_wins']
        # Calculate total forces using new commander-based method
        if not hasattr(self, '_unit_types'):
            self._unit_types = UnitTypeClassifier.load_unit_types(self.state)

        # Calculate using new commander-based method
        attacker_land, attacker_naval, defender_land, defender_naval, _, _ = self.calculate_total_army_size_by_country(war, self._unit_types)

        # Total forces = all land and naval units from both sides
        total_count = attacker_land + attacker_naval + defender_land + defender_naval

        # Calculate total casualties for both sides (LAND UNITS ONLY)
        casualties_count = 0
        for battle in war.battles:
            attacker_units = battle.attacker.get('units', {})
            defender_units = battle.defender.get('units', {})
            attacker_losses = battle.attacker.get("losses", 0)
            defender_losses = battle.defender.get("losses", 0)
            
            # Calculate land unit ratios for both sides
            attacker_land_units = 0
            defender_land_units = 0
            
            for unit_type, count in attacker_units.items():
                unit_category = self._unit_types.get(unit_type, 'infantry')
                if UnitTypeClassifier.is_land_unit(unit_category):
                    attacker_land_units += count
            
            for unit_type, count in defender_units.items():
                unit_category = self._unit_types.get(unit_type, 'infantry')
                if UnitTypeClassifier.is_land_unit(unit_category):
                    defender_land_units += count
            
            # Calculate total units for loss distribution
            attacker_total_units = sum(attacker_units.values())
            defender_total_units = sum(defender_units.values())
            
            # Distribute losses proportionally to land units
            if attacker_total_units > 0:
                attacker_land_ratio = attacker_land_units / attacker_total_units
            else:
                attacker_land_ratio = 0
                
            if defender_total_units > 0:
                defender_land_ratio = defender_land_units / defender_total_units
            else:
                defender_land_ratio = 0
            
            casualties_count += int(attacker_losses * attacker_land_ratio)
            casualties_count += int(defender_losses * defender_land_ratio)

        # Casualties icon
        if casualties_icon:
            photo_casualties_icon = ImageTk.PhotoImage(casualties_icon)
            self.image_refs.append(photo_casualties_icon)
            casualties_icon_id = self.canvas.create_image(detail_x + 219, stats_y_position, anchor=tk.NW, image=photo_casualties_icon)
            self._tab3_widgets.append(casualties_icon_id)

        # Land battles icon
        if army_icon:
            photo_army_icon = ImageTk.PhotoImage(army_icon)
            self.image_refs.append(photo_army_icon)
            army_icon_id = self.canvas.create_image(detail_x + 215, stats_y_position + 45, anchor=tk.NW, image=photo_army_icon)
            self._tab3_widgets.append(army_icon_id)

        # Column positions
        label_x = detail_x + 255      # Labels start here
        number_x = detail_x + 405     # Numbers aligned here

        # Total row
        total_img = self.font_manager.render_text("Total forces:", size=15, color="black")
        if total_img:
            # Crop transparent padding
            bbox = total_img.getbbox()
            if bbox:
                total_img = total_img.crop(bbox)
            total_photo = ImageTk.PhotoImage(total_img)
            self.image_refs.append(total_photo)
            self.canvas.create_image(label_x, stats_y_position + 5, anchor=tk.NW, image=total_photo)
        
        total_count_img = self.font_manager.render_text(f"{total_count:,}", size=15, color="black")
        if total_count_img:
            # Crop transparent padding for proper NE anchoring
            bbox = total_count_img.getbbox()
            if bbox:
                total_count_img = total_count_img.crop(bbox)
            total_count_photo = ImageTk.PhotoImage(total_count_img)
            self.image_refs.append(total_count_photo)
            self.canvas.create_image(number_x, stats_y_position + 5, anchor=tk.NE, image=total_count_photo)

        # Casualties row
        casualties_img = self.font_manager.render_text("Total deaths:", size=15, color="black")
        if casualties_img:
            # Crop transparent padding
            bbox = casualties_img.getbbox()
            if bbox:
                casualties_img = casualties_img.crop(bbox)
            casualties_photo = ImageTk.PhotoImage(casualties_img)
            self.image_refs.append(casualties_photo)
            self.canvas.create_image(label_x, stats_y_position + 19, anchor=tk.NW, image=casualties_photo)

        casualties_count_img = self.font_manager.render_text(f"{casualties_count:,}", size=15, color="black")
        if casualties_count_img:
            # Crop transparent padding for proper NE anchoring 
            bbox = casualties_count_img.getbbox()
            if bbox:
                casualties_count_img = casualties_count_img.crop(bbox)
            casualties_count_photo = ImageTk.PhotoImage(casualties_count_img)
            self.image_refs.append(casualties_count_photo)
            self.canvas.create_image(number_x, stats_y_position + 19, anchor=tk.NE, image=casualties_count_photo)
        
        # Land battles row
        land_battles_img = self.font_manager.render_text("Land Battles:", size=15, color="black")
        if land_battles_img:
            # Crop transparent padding
            bbox = land_battles_img.getbbox()
            if bbox:
                land_battles_img = land_battles_img.crop(bbox)
            land_battles_photo = ImageTk.PhotoImage(land_battles_img)
            self.image_refs.append(land_battles_photo)
            self.canvas.create_image(label_x, stats_y_position + 37, anchor=tk.NW, image=land_battles_photo)
        
        land_count_img = self.font_manager.render_text(str(land_battles_count), size=15, color="black")
        if land_count_img:
            # Crop transparent padding for proper NE anchoring
            bbox = land_count_img.getbbox()
            if bbox:
                land_count_img = land_count_img.crop(bbox)
            land_count_photo = ImageTk.PhotoImage(land_count_img)
            self.image_refs.append(land_count_photo)
            self.canvas.create_image(number_x, stats_y_position + 37, anchor=tk.NE, image=land_count_photo)

        # Attacker wins row
        atk_wins_img = self.font_manager.render_text("Attackers Won:", size=15, color="black")
        if atk_wins_img:
            # Crop transparent padding
            bbox = atk_wins_img.getbbox()
            if bbox:
                atk_wins_img = atk_wins_img.crop(bbox)
            atk_wins_photo = ImageTk.PhotoImage(atk_wins_img)
            self.image_refs.append(atk_wins_photo)
            self.canvas.create_image(label_x, stats_y_position + 51, anchor=tk.NW, image=atk_wins_photo)
        
        atk_count_img = self.font_manager.render_text(str(attacker_land_wins_count), size=15, color="black")
        if atk_count_img:
            # Crop transparent padding for proper NE anchoring
            bbox = atk_count_img.getbbox()
            if bbox:
                atk_count_img = atk_count_img.crop(bbox)
            atk_count_photo = ImageTk.PhotoImage(atk_count_img)
            self.image_refs.append(atk_count_photo)
            self.canvas.create_image(number_x, stats_y_position + 51, anchor=tk.NE, image=atk_count_photo)

        # Defender wins row
        def_wins_img = self.font_manager.render_text("Defenders Won:", size=15, color="black")
        if def_wins_img:
            # Crop transparent padding
            bbox = def_wins_img.getbbox()
            if bbox:
                def_wins_img = def_wins_img.crop(bbox)
            def_wins_photo = ImageTk.PhotoImage(def_wins_img)
            self.image_refs.append(def_wins_photo)
            self.canvas.create_image(label_x, stats_y_position + 65, anchor=tk.NW, image=def_wins_photo)
        
        def_count_img = self.font_manager.render_text(str(defender_land_wins_count), size=15, color="black")
        if def_count_img:
            # Crop transparent padding for proper NE anchoring
            bbox = def_count_img.getbbox()
            if bbox:
                def_count_img = def_count_img.crop(bbox)
            def_count_photo = ImageTk.PhotoImage(def_count_img)
            self.image_refs.append(def_count_photo)
            self.canvas.create_image(number_x, stats_y_position + 65, anchor=tk.NE, image=def_count_photo)

        # Naval battles summary
        naval_battles_count = len(naval_stats['naval_battles'])
        attacker_naval_wins_count = sum(1 for b in naval_stats['naval_battles'] if b.result is True)
        defender_naval_wins_count = sum(1 for b in naval_stats['naval_battles'] if b.result is False)

        # Naval battles icon
        if navy_icon:
            photo_navy_icon = ImageTk.PhotoImage(navy_icon)
            self.image_refs.append(photo_navy_icon)
            navy_icon_id = self.canvas.create_image(detail_x + 215, stats_y_position + 91, anchor=tk.NW, image=photo_navy_icon)
            self._tab3_widgets.append(navy_icon_id)

        # Naval battles row
        naval_battles_img = self.font_manager.render_text("Naval Battles:", size=15, color="black")
        if naval_battles_img:
            # Crop transparent padding
            bbox = naval_battles_img.getbbox()
            if bbox:
                naval_battles_img = naval_battles_img.crop(bbox)
            naval_battles_photo = ImageTk.PhotoImage(naval_battles_img)
            self.image_refs.append(naval_battles_photo)
            self.canvas.create_image(label_x, stats_y_position + 83, anchor=tk.NW, image=naval_battles_photo)
        
        naval_count_img = self.font_manager.render_text(str(naval_battles_count), size=15, color="black")
        if naval_count_img:
            # Crop transparent padding for proper NE anchoring
            bbox = naval_count_img.getbbox()
            if bbox:
                naval_count_img = naval_count_img.crop(bbox)
            naval_count_photo = ImageTk.PhotoImage(naval_count_img)
            self.image_refs.append(naval_count_photo)
            self.canvas.create_image(number_x, stats_y_position + 83, anchor=tk.NE, image=naval_count_photo)

        # Attacker naval wins row
        atk_naval_wins_img = self.font_manager.render_text("Attackers Won:", size=15, color="black")
        if atk_naval_wins_img:
            # Crop transparent padding
            bbox = atk_naval_wins_img.getbbox()
            if bbox:
                atk_naval_wins_img = atk_naval_wins_img.crop(bbox)
            atk_naval_wins_photo = ImageTk.PhotoImage(atk_naval_wins_img)
            self.image_refs.append(atk_naval_wins_photo)
            self.canvas.create_image(label_x, stats_y_position + 97, anchor=tk.NW, image=atk_naval_wins_photo)
        
        atk_naval_count_img = self.font_manager.render_text(str(attacker_naval_wins_count), size=15, color="black")
        if atk_naval_count_img:
            # Crop transparent padding for proper NE anchoring
            bbox = atk_naval_count_img.getbbox()
            if bbox:
                atk_naval_count_img = atk_naval_count_img.crop(bbox)
            atk_naval_count_photo = ImageTk.PhotoImage(atk_naval_count_img)
            self.image_refs.append(atk_naval_count_photo)
            self.canvas.create_image(number_x, stats_y_position + 97, anchor=tk.NE, image=atk_naval_count_photo)

        # Defender naval wins row
        def_naval_wins_img = self.font_manager.render_text("Defenders Won:", size=15, color="black")
        if def_naval_wins_img:
            # Crop transparent padding
            bbox = def_naval_wins_img.getbbox()
            if bbox:
                def_naval_wins_img = def_naval_wins_img.crop(bbox)
            def_naval_wins_photo = ImageTk.PhotoImage(def_naval_wins_img)
            self.image_refs.append(def_naval_wins_photo)
            self.canvas.create_image(label_x, stats_y_position + 111, anchor=tk.NW, image=def_naval_wins_photo)
        
        def_naval_count_img = self.font_manager.render_text(str(defender_naval_wins_count), size=15, color="black")
        if def_naval_count_img:
            # Crop transparent padding for proper NE anchoring
            bbox = def_naval_count_img.getbbox()
            if bbox:
                def_naval_count_img = def_naval_count_img.crop(bbox)
            def_naval_count_photo = ImageTk.PhotoImage(def_naval_count_img)
            self.image_refs.append(def_naval_count_photo)
            self.canvas.create_image(number_x, stats_y_position + 111, anchor=tk.NE, image=def_naval_count_photo)

        # ===== SIDE MILITARY STATISTICS (ARMY SIZE, LOSSES, SHIPS, etc.) =====
        # Position the side stats lower to make room for the battle win statistics
        side_stats_y_position = stats_y_position + 47  # Increased spacing

        # Draw attacker military stats (left side)
        self._draw_side_military_stats(war, detail_x, side_stats_y_position, is_attacker=True, 
                                    land_stats=land_stats, naval_stats=naval_stats)

        # Draw defender military stats (right side)  
        self._draw_side_military_stats(war, detail_x + 424, side_stats_y_position, is_attacker=False,
                                    land_stats=land_stats, naval_stats=naval_stats)

        # Calculate war statistics to get the war score
        stats = WarAnalyzer.calculate_war_statistics(war)
        self._draw_war_score(self.canvas, stats.war_score_estimate, detail_x + 313, detail_y + 30)

        # ===== WAR GOALS DISPLAY - RESTORE ORIGINAL POSITION =====
        if war.goals:
            war_goal = war.goals[0]
            translated_goal = self._translate_war_goal(war_goal, war)
            cb_name = war_goal.casus_belli
            
            # ===== CB ICON =====
            if cb_name:
                cb_name_for_icon = cb_name.replace('_small', '')
                cb_icon = self._get_cb_icon(cb_name_for_icon)
                if cb_icon:
                    # Ensure transparency
                    if cb_icon.mode != 'RGBA':
                        cb_icon = cb_icon.convert('RGBA')
                    
                    # RESTORE ORIGINAL SIZE (24x24)
                    cb_icon = cb_icon.resize((24, 24), Image.Resampling.LANCZOS)
                    
                    photo_icon = ImageTk.PhotoImage(cb_icon)
                    self.image_refs.append(photo_icon)
                    
                    # ORIGINAL POSITION: to the left of goal text
                    icon_x_position = detail_x + 176
                    icon_y_position = detail_y + 80
                    icon_id = self.canvas.create_image(icon_x_position, icon_y_position, 
                                                    anchor=tk.NW, image=photo_icon)
                    self._tab3_widgets.append(icon_id)
            
            # ===== GOAL TEXT (SEPARATE) =====
            # Render goal text using BMFont
            goal_img = self.font_manager.render_bold_text(translated_goal, size=12, color="white")
            if goal_img is not None:
                goal_photo = ImageTk.PhotoImage(goal_img)
                self.image_refs.append(goal_photo)
                
                # ADJUST TEXT POSITION HERE - MOVED FURTHER RIGHT
                goal_text_x_position = WINDOW_WIDTH // 2  # Position next to icon
                goal_text_y_position = detail_y - 2   # Same Y as icon
                goal_id = self.canvas.create_image(goal_text_x_position, goal_text_y_position, anchor=tk.N, image=goal_photo)
                self._tab3_widgets.append(goal_id)
                                
        # ===== COMPACT SUMMARY WITHOUT DATE OR GOALS =====
        # Create compact summary frame (positioned below war goals)
        summary_frame = tk.Frame(self.root, bg=BG_COLOR)
        
        # Calculate summary position based on number of war goals
        summary_y_position = detail_y + 188

        frame_wid = self.canvas.create_window(detail_x, summary_y_position, anchor=tk.NW, window=summary_frame)
        self._tab3_widgets.append(frame_wid)
        
        # ===== BATTLE LIST WITH SORT BUTTONS =====
        if war.battles:
            # Draw battle sort buttons INSTEAD OF the "Battles:" label
            self._draw_battle_sort_buttons()
            
            # Create scrollable battles list below sort buttons
            battles_scrollable = ScrollableList(self.canvas, self.root, self.state, 
                                            detail_x - 3, summary_y_position + 60,  # Adjust Y position if needed
                                            639, 287, self.scrollbar_assets, row_height=24)
            battles_scrollable.create()
            
            # Load battle row background
            battle_bg_path = self.state.get_modded_path(os.path.join("gfx", "interface", "diplo_countrybutton.dds"))
            battle_row_bg = self._load_battle_row_background(battle_bg_path)
            
            for i, battle in enumerate(war.battles):
                self._draw_battle_row(battles_scrollable.frame_inside, battle, i, battle_row_bg)
        else:
            # Render "No battles" text using BMFont
            no_battles_img = self.font_manager.render_text("No battles recorded", size=15, color="black")
            if no_battles_img:
                no_battles_photo = ImageTk.PhotoImage(no_battles_img)
                self.image_refs.append(no_battles_photo)
                no_battles_id = self.canvas.create_image(detail_x, summary_y_position + 50, anchor=tk.NW, image=no_battles_photo)
                self._tab3_widgets.append(no_battles_id)

        end_image_count = len(self.image_refs)

    def _calculate_land_statistics(self, war: War) -> Dict:
        """Calculate land battle statistics - ONLY COUNTING LAND UNITS."""
        # Load unit types to properly identify land battles
        if not hasattr(self, '_unit_types'):
            self._unit_types = UnitTypeClassifier.load_unit_types(self.state)
        
        land_battles = []
        attacker_land_wins = 0
        defender_land_wins = 0
        
        for battle in war.battles:
            # Check if this is primarily a land battle by counting unit types
            attacker_units = battle.attacker.get('units', {})
            defender_units = battle.defender.get('units', {})
            
            land_units_count = 0
            naval_units_count = 0
            
            # Count land vs naval units for both sides
            for unit_type, count in attacker_units.items():
                unit_category = self._unit_types.get(unit_type, 'infantry')
                if UnitTypeClassifier.is_land_unit(unit_category):
                    land_units_count += count
                elif UnitTypeClassifier.is_naval_unit(unit_category):
                    naval_units_count += count
            
            for unit_type, count in defender_units.items():
                unit_category = self._unit_types.get(unit_type, 'infantry')
                if UnitTypeClassifier.is_land_unit(unit_category):
                    land_units_count += count
                elif UnitTypeClassifier.is_naval_unit(unit_category):
                    naval_units_count += count
            
            # Consider it a land battle if it has more land units than naval units
            # OR if it has any land units at all (mixed battles count as land)
            if land_units_count > 0 and land_units_count >= naval_units_count:
                land_battles.append(battle)
                if battle.result is True:
                    attacker_land_wins += 1
                elif battle.result is False:
                    defender_land_wins += 1
        
        return {
            'land_battles': land_battles,
            'attacker_land_wins': attacker_land_wins,
            'defender_land_wins': defender_land_wins
        }

    def _calculate_naval_statistics(self, war: War) -> Dict:
        """Calculate naval battle statistics - ONLY COUNTING NAVAL UNITS."""
        # Load unit types to properly identify naval battles
        if not hasattr(self, '_unit_types'):
            self._unit_types = UnitTypeClassifier.load_unit_types(self.state)
        
        naval_battles = []
        attacker_naval_wins = 0
        defender_naval_wins = 0
        
        for battle in war.battles:
            # Check if this is primarily a naval battle by counting unit types
            attacker_units = battle.attacker.get('units', {})
            defender_units = battle.defender.get('units', {})
            
            land_units_count = 0
            naval_units_count = 0
            
            # Count land vs naval units for both sides
            for unit_type, count in attacker_units.items():
                unit_category = self._unit_types.get(unit_type, 'infantry')
                if UnitTypeClassifier.is_land_unit(unit_category):
                    land_units_count += count
                elif UnitTypeClassifier.is_naval_unit(unit_category):
                    naval_units_count += count
            
            for unit_type, count in defender_units.items():
                unit_category = self._unit_types.get(unit_type, 'infantry')
                if UnitTypeClassifier.is_land_unit(unit_category):
                    land_units_count += count
                elif UnitTypeClassifier.is_naval_unit(unit_category):
                    naval_units_count += count
            
            # Consider it a naval battle if it has more naval units than land units
            # OR if it has any naval units at all (mixed battles count as naval if navy dominates)
            if naval_units_count > 0 and naval_units_count >= land_units_count:
                naval_battles.append(battle)
                if battle.result is True:
                    attacker_naval_wins += 1
                elif battle.result is False:
                    defender_naval_wins += 1
        
        # Rest of the ship type detection code remains the same...
        has_dreadnoughts = False
        has_battleships = False
        has_ironclads = False
        
        for battle in naval_battles:
            attacker_units = battle.attacker.get('units', {})
            defender_units = battle.defender.get('units', {})
            
            # Check for ship types
            if attacker_units.get('dreadnought', 0) > 0 or defender_units.get('dreadnought', 0) > 0:
                has_dreadnoughts = True
            if attacker_units.get('battleship', 0) > 0 or defender_units.get('battleship', 0) > 0:
                has_battleships = True
            if attacker_units.get('ironclad', 0) > 0 or defender_units.get('ironclad', 0) > 0:
                has_ironclads = True
        
        # Determine crop coordinates based on ship types
        if has_dreadnoughts:
            crop_left, crop_right = 324, 0
        elif has_battleships:
            crop_left, crop_right = 288, 36
        elif has_ironclads:
            crop_left, crop_right = 180, 144
        else:
            crop_left, crop_right = 0, 324
        
        return {
            'naval_battles': naval_battles,
            'attacker_naval_wins': attacker_naval_wins,
            'defender_naval_wins': defender_naval_wins,
            'ship_crop_left': crop_left,
            'ship_crop_right': crop_right
        }
    
    def calculate_total_army_size_by_country(self, war: War, unit_types: Dict[str, str]):
        """Track land and naval units separately by country-commander pairs."""
        
        # {country_tag: {commander_name: {land_peak, naval_peak}}}
        country_commander_peaks = {}
        # {country_tag: {commander_name: {land_survivors, naval_survivors}}}
        country_commander_survivors = {}
        
        total_attacker_land = 0
        total_attacker_naval = 0
        total_defender_land = 0
        total_defender_naval = 0
        
        sorted_battles = sorted(war.battles, key=lambda b: b.start_date if hasattr(b, 'start_date') else 0)
        
        for battle in sorted_battles:
            # Process attacker
            attacker_country = battle.attacker.get('country', 'UNK')
            attacker_leader = battle.attacker.get('leader', 'Unknown')
            attacker_units = battle.attacker.get('units', {})
            attacker_losses = battle.attacker.get('losses', 0)
            
            # Calculate land and naval units separately
            attacker_land_units = 0
            attacker_naval_units = 0
            
            for unit_type, count in attacker_units.items():
                unit_category = unit_types.get(unit_type, 'infantry')
                if UnitTypeClassifier.is_land_unit(unit_category):
                    attacker_land_units += count
                elif UnitTypeClassifier.is_naval_unit(unit_category):
                    attacker_naval_units += count
            
            # Calculate survivors (distribute losses proportionally)
            total_attacker_units = attacker_land_units + attacker_naval_units
            if total_attacker_units > 0:
                land_loss_ratio = attacker_land_units / total_attacker_units
                naval_loss_ratio = attacker_naval_units / total_attacker_units
            else:
                land_loss_ratio = 1.0
                naval_loss_ratio = 0.0
                
            attacker_land_survivors = max(0, attacker_land_units - int(attacker_losses * land_loss_ratio))
            attacker_naval_survivors = max(0, attacker_naval_units - int(attacker_losses * naval_loss_ratio))
            
            if attacker_country != 'UNK' and attacker_leader != "Unknown":
                country_key = attacker_country
                if country_key not in country_commander_peaks:
                    country_commander_peaks[country_key] = {}
                    country_commander_survivors[country_key] = {}
                
                current_land_peak = country_commander_peaks[country_key].get(attacker_leader, {}).get('land', 0)
                current_naval_peak = country_commander_peaks[country_key].get(attacker_leader, {}).get('naval', 0)
                last_land_survivors = country_commander_survivors[country_key].get(attacker_leader, {}).get('land', 0)
                last_naval_survivors = country_commander_survivors[country_key].get(attacker_leader, {}).get('naval', 0)
                
                # Track land reinforcements
                if attacker_land_units > last_land_survivors:
                    new_land_reinforcements = attacker_land_units - last_land_survivors
                    total_attacker_land += new_land_reinforcements
                    if attacker_leader not in country_commander_peaks[country_key]:
                        country_commander_peaks[country_key][attacker_leader] = {}
                    country_commander_peaks[country_key][attacker_leader]['land'] = current_land_peak + new_land_reinforcements
                
                # Track naval reinforcements  
                if attacker_naval_units > last_naval_survivors:
                    new_naval_reinforcements = attacker_naval_units - last_naval_survivors
                    total_attacker_naval += new_naval_reinforcements
                    if attacker_leader not in country_commander_peaks[country_key]:
                        country_commander_peaks[country_key][attacker_leader] = {}
                    country_commander_peaks[country_key][attacker_leader]['naval'] = current_naval_peak + new_naval_reinforcements
                
                # Update survivors
                if attacker_leader not in country_commander_survivors[country_key]:
                    country_commander_survivors[country_key][attacker_leader] = {}
                country_commander_survivors[country_key][attacker_leader]['land'] = attacker_land_survivors
                country_commander_survivors[country_key][attacker_leader]['naval'] = attacker_naval_survivors
            
            # Process defender (similar logic)
            defender_country = battle.defender.get('country', 'UNK')
            defender_leader = battle.defender.get('leader', 'Unknown')
            defender_units = battle.defender.get('units', {})
            defender_losses = battle.defender.get('losses', 0)
            
            defender_land_units = 0
            defender_naval_units = 0
            
            for unit_type, count in defender_units.items():
                unit_category = unit_types.get(unit_type, 'infantry')
                if UnitTypeClassifier.is_land_unit(unit_category):
                    defender_land_units += count
                elif UnitTypeClassifier.is_naval_unit(unit_category):
                    defender_naval_units += count
            
            total_defender_units = defender_land_units + defender_naval_units
            if total_defender_units > 0:
                land_loss_ratio = defender_land_units / total_defender_units
                naval_loss_ratio = defender_naval_units / total_defender_units
            else:
                land_loss_ratio = 1.0
                naval_loss_ratio = 0.0
                
            defender_land_survivors = max(0, defender_land_units - int(defender_losses * land_loss_ratio))
            defender_naval_survivors = max(0, defender_naval_units - int(defender_losses * naval_loss_ratio))
            
            if defender_country != 'UNK' and defender_leader != "Unknown":
                country_key = defender_country
                if country_key not in country_commander_peaks:
                    country_commander_peaks[country_key] = {}
                    country_commander_survivors[country_key] = {}
                
                current_land_peak = country_commander_peaks[country_key].get(defender_leader, {}).get('land', 0)
                current_naval_peak = country_commander_peaks[country_key].get(defender_leader, {}).get('naval', 0)
                last_land_survivors = country_commander_survivors[country_key].get(defender_leader, {}).get('land', 0)
                last_naval_survivors = country_commander_survivors[country_key].get(defender_leader, {}).get('naval', 0)
                
                if defender_land_units > last_land_survivors:
                    new_land_reinforcements = defender_land_units - last_land_survivors
                    total_defender_land += new_land_reinforcements
                    if defender_leader not in country_commander_peaks[country_key]:
                        country_commander_peaks[country_key][defender_leader] = {}
                    country_commander_peaks[country_key][defender_leader]['land'] = current_land_peak + new_land_reinforcements
                
                if defender_naval_units > last_naval_survivors:
                    new_naval_reinforcements = defender_naval_units - last_naval_survivors
                    total_defender_naval += new_naval_reinforcements
                    if defender_leader not in country_commander_peaks[country_key]:
                        country_commander_peaks[country_key][defender_leader] = {}
                    country_commander_peaks[country_key][defender_leader]['naval'] = current_naval_peak + new_naval_reinforcements
                
                if defender_leader not in country_commander_survivors[country_key]:
                    country_commander_survivors[country_key][defender_leader] = {}
                country_commander_survivors[country_key][defender_leader]['land'] = defender_land_survivors
                country_commander_survivors[country_key][defender_leader]['naval'] = defender_naval_survivors
        
        return total_attacker_land, total_attacker_naval, total_defender_land, total_defender_naval, country_commander_peaks, country_commander_survivors

    def _draw_side_military_stats(self, war: War, x: int, y: int, is_attacker: bool, 
                                land_stats: Dict, naval_stats: Dict):
        """Calculate military statistics using commander-based reinforcement tracking"""
        if not hasattr(self, '_unit_types'):
            self._unit_types = UnitTypeClassifier.load_unit_types(self.state)
        
        # Track each commander's cumulative reinforcements and survivors
        commander_peak_units = {}  # {commander_name: total_men_served_under_this_commander}
        commander_survivors = {}   # {commander_name: survivors_from_last_battle}
        
        total_land_army = 0
        total_naval_ships = 0
        total_land_losses = 0
        total_naval_losses = 0
        
        # Sort battles by date to track reinforcement flow
        sorted_battles = sorted(war.battles, key=lambda b: getattr(b, 'start_date', ''))
        
        for battle in sorted_battles:
            # Process attacker
            attacker_leader = battle.attacker.get('leader', 'Unknown')
            attacker_units = battle.attacker.get('units', {})
            attacker_losses = battle.attacker.get('losses', 0)
            
            # Calculate land and naval units for this battle
            attacker_land_units = 0
            attacker_naval_units = 0
            
            for unit_type, count in attacker_units.items():
                unit_category = self._unit_types.get(unit_type, 'infantry')
                if UnitTypeClassifier.is_land_unit(unit_category):
                    attacker_land_units += count
                elif UnitTypeClassifier.is_naval_unit(unit_category):
                    attacker_naval_units += count
            
            # Calculate survivors
            total_attacker_units = attacker_land_units + attacker_naval_units
            if total_attacker_units > 0:
                land_loss_ratio = attacker_land_units / total_attacker_units
                naval_loss_ratio = attacker_naval_units / total_attacker_units
            else:
                land_loss_ratio = 0.5
                naval_loss_ratio = 0.5
                
            attacker_land_survivors = max(0, attacker_land_units - int(attacker_losses * land_loss_ratio))
            attacker_naval_survivors = max(0, attacker_naval_units - int(attacker_losses * naval_loss_ratio))
            
            if attacker_leader != "Unknown" and is_attacker:
                last_land_survivors = commander_survivors.get(attacker_leader, {}).get('land', 0)
                last_naval_survivors = commander_survivors.get(attacker_leader, {}).get('naval', 0)
                
                # YOUR LOGIC: Only count reinforcements beyond what survived from last battle
                if attacker_land_units > last_land_survivors:
                    new_land_reinforcements = attacker_land_units - last_land_survivors
                    total_land_army += new_land_reinforcements
                
                if attacker_naval_units > last_naval_survivors:
                    new_naval_reinforcements = attacker_naval_units - last_naval_survivors
                    total_naval_ships += new_naval_reinforcements
                
                # Update survivors for next battle
                if attacker_leader not in commander_survivors:
                    commander_survivors[attacker_leader] = {}
                commander_survivors[attacker_leader]['land'] = attacker_land_survivors
                commander_survivors[attacker_leader]['naval'] = attacker_naval_survivors
            
            # Process defender
            defender_leader = battle.defender.get('leader', 'Unknown')
            defender_units = battle.defender.get('units', {})
            defender_losses = battle.defender.get('losses', 0)
            
            defender_land_units = 0
            defender_naval_units = 0
            
            for unit_type, count in defender_units.items():
                unit_category = self._unit_types.get(unit_type, 'infantry')
                if UnitTypeClassifier.is_land_unit(unit_category):
                    defender_land_units += count
                elif UnitTypeClassifier.is_naval_unit(unit_category):
                    defender_naval_units += count
            
            total_defender_units = defender_land_units + defender_naval_units
            if total_defender_units > 0:
                land_loss_ratio = defender_land_units / total_defender_units
                naval_loss_ratio = defender_naval_units / total_defender_units
            else:
                land_loss_ratio = 0.5
                naval_loss_ratio = 0.5
                
            defender_land_survivors = max(0, defender_land_units - int(defender_losses * land_loss_ratio))
            defender_naval_survivors = max(0, defender_naval_units - int(defender_losses * naval_loss_ratio))
            
            if defender_leader != "Unknown" and not is_attacker:
                last_land_survivors = commander_survivors.get(defender_leader, {}).get('land', 0)
                last_naval_survivors = commander_survivors.get(defender_leader, {}).get('naval', 0)
                
                if defender_land_units > last_land_survivors:
                    new_land_reinforcements = defender_land_units - last_land_survivors
                    total_land_army += new_land_reinforcements
                
                if defender_naval_units > last_naval_survivors:
                    new_naval_reinforcements = defender_naval_units - last_naval_survivors
                    total_naval_ships += new_naval_reinforcements
                
                if defender_leader not in commander_survivors:
                    commander_survivors[defender_leader] = {}
                commander_survivors[defender_leader]['land'] = defender_land_survivors
                commander_survivors[defender_leader]['naval'] = defender_naval_survivors
            
            # Accumulate losses for this side
            if is_attacker:
                total_land_losses += int(attacker_losses * land_loss_ratio)
                total_naval_losses += int(attacker_losses * naval_loss_ratio)
            else:
                total_land_losses += int(defender_losses * land_loss_ratio)
                total_naval_losses += int(defender_losses * naval_loss_ratio)
        
        # [REST OF YOUR DRAWING CODE - same as before]
        # Draw land army icon and text
        army_icon_path = self.state.get_modded_path(os.path.join("gfx", "interface", "pops_mini.dds"))
        army_icon = SafeLoader.safe_load_image(army_icon_path)
        if army_icon:
            army_cropped = army_icon.crop((192, 0, 192 + 32, 32))
            photo_army = ImageTk.PhotoImage(army_cropped)
            self.image_refs.append(photo_army)
            self.canvas.create_image(x, y - 2, anchor=tk.NW, image=photo_army)
        
        label_x = x + 35
        number_x = x + 195

        # Army size
        army_size_label = self.font_manager.render_text("Army size:", size=15, color="black")
        if army_size_label:
            bbox = army_size_label.getbbox()
            if bbox:
                army_size_label = army_size_label.crop(bbox)
            army_size_photo = ImageTk.PhotoImage(army_size_label)
            self.image_refs.append(army_size_photo)
            self.canvas.create_image(label_x, y + 4, anchor=tk.NW, image=army_size_photo)
        
        army_size_number = self.font_manager.render_text(f"{total_land_army:,}", size=15, color="black")
        if army_size_number:
            bbox = army_size_number.getbbox()
            if bbox:
                army_size_number = army_size_number.crop(bbox)
            army_size_num_photo = ImageTk.PhotoImage(army_size_number)
            self.image_refs.append(army_size_num_photo)
            self.canvas.create_image(number_x, y + 4, anchor=tk.NE, image=army_size_num_photo)

        # Army losses
        losses_label = self.font_manager.render_text("Losses:", size=15, color="black")
        if losses_label:
            bbox = losses_label.getbbox()
            if bbox:
                losses_label = losses_label.crop(bbox)
            losses_photo = ImageTk.PhotoImage(losses_label)
            self.image_refs.append(losses_photo)
            self.canvas.create_image(label_x, y + 18, anchor=tk.NW, image=losses_photo)
        
        losses_number = self.font_manager.render_text(f"{total_land_losses:,}", size=15, color="black")
        if losses_number:
            bbox = losses_number.getbbox()
            if bbox:
                losses_number = losses_number.crop(bbox)
            losses_num_photo = ImageTk.PhotoImage(losses_number)
            self.image_refs.append(losses_num_photo)
            self.canvas.create_image(number_x, y + 18, anchor=tk.NE, image=losses_num_photo)

        # Draw naval ships
        ships_icon_path = self.state.get_modded_path(os.path.join("gfx", "interface", "unit_strip_naval_combat_1_R.dds"))
        ships_icon = SafeLoader.safe_load_image(ships_icon_path)
        if ships_icon:
            crop_left = naval_stats['ship_crop_left']
            crop_right = naval_stats['ship_crop_right']
            ships_cropped = ships_icon.crop((crop_left, 0, 360 - crop_right, 36))
            ships_cropped = ships_cropped.resize((32, 32), Image.Resampling.LANCZOS)
            photo_ships = ImageTk.PhotoImage(ships_cropped)
            self.image_refs.append(photo_ships)
            self.canvas.create_image(x, y + 46, anchor=tk.NW, image=photo_ships)

        # Navy size
        navy_size_label = self.font_manager.render_text("Navy size:", size=15, color="black")
        if navy_size_label:
            bbox = navy_size_label.getbbox()
            if bbox:
                navy_size_label = navy_size_label.crop(bbox)
            navy_size_photo = ImageTk.PhotoImage(navy_size_label)
            self.image_refs.append(navy_size_photo)
            self.canvas.create_image(label_x, y + 50, anchor=tk.NW, image=navy_size_photo)
        
        navy_size_number = self.font_manager.render_text(f"{total_naval_ships:,}", size=15, color="black")
        if navy_size_number:
            bbox = navy_size_number.getbbox()
            if bbox:
                navy_size_number = navy_size_number.crop(bbox)
            navy_size_num_photo = ImageTk.PhotoImage(navy_size_number)
            self.image_refs.append(navy_size_num_photo)
            self.canvas.create_image(number_x, y + 50, anchor=tk.NE, image=navy_size_num_photo)

        # Navy sunk
        sunk_label = self.font_manager.render_text("Sunk:", size=15, color="black")
        if sunk_label:
            bbox = sunk_label.getbbox()
            if bbox:
                sunk_label = sunk_label.crop(bbox)
            sunk_photo = ImageTk.PhotoImage(sunk_label)
            self.image_refs.append(sunk_photo)
            self.canvas.create_image(label_x, y + 64, anchor=tk.NW, image=sunk_photo)
        
        sunk_number = self.font_manager.render_text(f"{total_naval_losses:,}", size=15, color="black")
        if sunk_number:
            bbox = sunk_number.getbbox()
            if bbox:
                sunk_number = sunk_number.crop(bbox)
            sunk_num_photo = ImageTk.PhotoImage(sunk_number)
            self.image_refs.append(sunk_num_photo)
            self.canvas.create_image(number_x, y + 64, anchor=tk.NE, image=sunk_num_photo)

    def _create_button(self, text: str, command, width: int = 144, height: int = 36) -> tk.Frame:
        """Create button with text rendered as separate layer above button image"""
        # Create a frame to hold the canvas
        frame = tk.Frame(self.root, bg=BG_COLOR, borderwidth=0, highlightthickness=0)
        
        # Load button image
        btn_path = self.state.get_modded_path(os.path.join("gfx", "interface", "button_standard_144.tga"))
        btn_img = SafeLoader.safe_load_image(btn_path, size=(width, height))
        
        if not btn_img:
            # Fallback to normal button
            btn = tk.Button(frame, text=text, command=command)
            btn.pack()
            return frame
        
        # Create canvas with exact button size
        canvas = tk.Canvas(frame, width=width, height=height, bg=BG_COLOR, 
                        borderwidth=0, highlightthickness=0)
        canvas.pack()
        
        # Convert button to PhotoImage and add to canvas
        btn_photo = ImageTk.PhotoImage(btn_img)
        self.image_refs.append(btn_photo)
        canvas.create_image(0, 0, anchor=tk.NW, image=btn_photo)
        
        # Render text using FontManager
        text_image = self.font_manager.render_text(text, 18, "black")

        # Crop ALL transparent padding from the text image
        bbox = text_image.getbbox()  # Gets (left, top, right, bottom) of non-transparent area
        if bbox:
            text_image = text_image.crop(bbox)

        # Now center the properly cropped image
        text_photo = ImageTk.PhotoImage(text_image)
        self.image_refs.append(text_photo)

        text_x = width // 2
        text_y = height // 2
        canvas.create_image(text_x, text_y, anchor=tk.CENTER, image=text_photo)
        
        # Make canvas clickable
        canvas.bind("<Button-1>", lambda e: command())
        canvas.configure(cursor="hand2")
        
        return frame

    def _calculate_image_sharpness(self, image: Image.Image) -> float:
        """Very simple sharpness calculation"""
        if image.mode != 'L':
            image = image.convert('L')
        
        import numpy as np
        arr = np.array(image)
        
        # Simple edge detection (Laplacian)
        gy, gx = np.gradient(arr)
        sharpness = np.sqrt(gx**2 + gy**2).mean()
        
        return sharpness
    
    def _draw_tabs(self):
        """Draw tab buttons directly on the main canvas."""
        tab_width = 144
        tab_height = 25
        baseline_y = 2

        for i, (key, tab_data) in enumerate(self.tab_definitions.items()):
            x = 29 + i * (tab_width + 16)
            y = 23

            # Render text using BMFont
            text = tab_data["label"]
            text_image = self.font_manager.render_text(text, size=22, color="black")
            
            if text_image is None:
                continue
                
            # Find the actual text bounds for proper centering
            bbox = text_image.getbbox()
            if bbox:
                text_content_width = bbox[2] - bbox[0]
                text_content_center_x = bbox[0] + (text_content_width // 2)
                content_offset_x = (text_image.width // 2) - text_content_center_x
                
                # Calculate centered position
                text_x = x + (tab_width // 2) + content_offset_x
            else:
                text_x = x + (tab_width // 2)
            
            # Convert text to PhotoImage
            text_photo = ImageTk.PhotoImage(text_image)
            self.image_refs.append(text_photo)
            
            # Add text directly to the main canvas (no separate canvas)
            text_y = y + baseline_y
            text_id = self.canvas.create_image(text_x, text_y, anchor=tk.N, image=text_photo, tags=(f"tab_{key}", "tab_text"))
            
            # Make the text clickable
            self.canvas.tag_bind(text_id, "<Button-1>", lambda e, k=key: self._on_tab_click(k))
            
            # Add a clickable area around the text (optional, for better UX)
            click_padding = 10
            click_area = self.canvas.create_rectangle(
                x - click_padding, y - click_padding,
                x + tab_width + click_padding, y + tab_height + click_padding,
                outline="", fill="", tags=(f"tab_{key}", "tab_clickable")
            )
            self.canvas.tag_bind(click_area, "<Button-1>", lambda e, k=key: self._on_tab_click(k))
    
    def _draw_tab_content(self, tab_name: str):
        """Draw content for a specific tab with selective cleanup."""
        # SELECTIVE CLEANUP - preserve UI elements
        try:
            # Delete all canvas items EXCEPT UI elements
            all_items = self.canvas.find_all()
            for item in all_items:
                tags = self.canvas.gettags(item)
                # Keep items tagged as UI, delete others
                if not any(tag in ['ui', 'background', 'tab'] for tag in tags):
                    self.canvas.delete(item)
            
            # Clear only war detail widgets
            if hasattr(self, "_tab3_widgets"):
                self._tab3_widgets.clear()
            
            # Clear current tab's temporary image references but preserve UI images
            current_refs = self.image_refs.copy()
            self.image_refs.clear()
            
            # Keep UI-related images
            for ref in current_refs:
                if any(ui_key in str(ref) for ui_key in ['layer_', 'bg_', 'button_']):
                    self.image_refs.append(ref)
            
            # Only do emergency cleanup when switching FROM war details
            if self.current_tab == "Tab3":
                self.emergency_cleanup()
            
        except Exception as e:
            pass

        # Now draw new content - layers should still be cached
        layer_items = self.layer_cache.get(tab_name)
        if layer_items:
            for photo, pos in layer_items:
                try:
                    self.canvas.create_image(*pos, anchor=tk.NW, image=photo)
                    self.image_refs.append(photo)
                except Exception:
                    pass

        self._draw_tabs()

        if tab_name == "Tab1":
            self._draw_settings_tab()
        elif tab_name == "Tab2":
            self._draw_wars_tab()
        elif tab_name == "Tab3":
            self._draw_war_details_tab()

    def _clear_mod_caches(self):
        """Clear all caches that contain mod-specific data."""
        # Clear image cache
        self.state.image_cache.clear()

        # Clear all file parsing caches
        self.state._localization_cache = None
        self.state._government_cache_loaded = False
        self.state._culture_mappings_cache = None
        self.state._cb_types_cache = None
        self.state._unit_types_cache = None
        
        # Clear government data
        self.state.gov_to_flagtype = {}
        self.state.gov_index_map = {}
        
        # Clear country data
        self.state._parsed_great_nations = None
        self.state._parsed_country_governments = {}
        self.state._parsed_country_cultures = {}
        
        # Clear localization
        self.state.localization_names = {}
        
        # Clear wars data (since countries might be different)
        self.state._wars_data = None
        self.state._save_file_text = None
        self.state.filtered_wars = []
        
        # Clear country rankings and selections
        self.state.country_rankings = {}
        self.state.selected_countries.clear()
        self.state.selected_war_index = None
        
        # Clear CB icons
        if hasattr(self, '_cb_icons'):
            self._cb_icons.clear()
        if hasattr(self, '_cb_icons_loaded'):
            self._cb_icons_loaded = False
        if hasattr(self, 'cb_to_sprite'):
            self.cb_to_sprite.clear()
        
        # Clear unit types cache
        if hasattr(self, '_unit_types'):
            delattr(self, '_unit_types')
        
        # Clear culture mappings
        if hasattr(self.state, '_culture_to_leader'):
            delattr(self.state, '_culture_to_leader')
        
        # Clear flag tooltip cache
        if hasattr(self, '_flag_tooltip_cache'):
            self._flag_tooltip_cache.clear()
    
    def _draw_settings_tab(self):
        """Draw the settings tab content with multiple mod support."""
        # Mod selector - UPDATED FOR MULTIPLE MODS
        def choose_mods():
            # Create mod selection dialog - exact size of background image
            selection_dialog = tk.Toplevel(self.root)
            selection_dialog.title("Mod Manager")
            selection_dialog.geometry("700x468")  # Exact size of unit_reorg_bg.dds
            selection_dialog.transient(self.root)
            selection_dialog.grab_set()
            selection_dialog.configure(bg=BG_COLOR)
            selection_dialog.resizable(False, False)  # Prevent resizing
            
            # ADD THIS: Set icon for the Mod Manager window
            vic2_icon_path = self.state.get_modded_path("Victoria2.ico")
            if os.path.exists(vic2_icon_path):
                selection_dialog.iconbitmap(vic2_icon_path)
            else:
                # Fallback: try the base Victoria 2 directory
                base_icon_path = os.path.join(self.state.vic2_path, "Victoria2.ico")
                if os.path.exists(base_icon_path):
                    selection_dialog.iconbitmap(base_icon_path)
            
            # Create canvas for background and graphics - exactly 700x468
            canvas = tk.Canvas(selection_dialog, width=700, height=468, highlightthickness=0, bg=BG_COLOR)
            canvas.pack(fill=tk.BOTH, expand=True)
            
            # Load and display background image - full window coverage
            bg_path = self.state.get_modded_path(os.path.join("gfx", "interface", "unit_reorg_bg.dds"))
            bg_image = SafeLoader.safe_load_image(bg_path, (700, 468))  # Exact window size
            
            if bg_image:
                bg_photo = ImageTk.PhotoImage(bg_image)
                # Cover the entire window
                canvas.create_image(0, 0, anchor=tk.NW, image=bg_photo)
                # Keep reference to prevent garbage collection
                selection_dialog.bg_photo = bg_photo
            
            # Title - directly on canvas
            title_img = self.font_manager.render_bold_text("Select Mods", size=22, color="white")
            title_photo = ImageTk.PhotoImage(title_img)
            canvas.create_image(350, 40, anchor=tk.CENTER, image=title_photo)
            selection_dialog.title_photo = title_photo
            
            # Get all folders in the mod directory
            available_mods_with_info = ModManager.get_available_mods_with_info(self.state.vic2_path)
            
            # Labels - directly on canvas for transparency
            available_label_img = self.font_manager.render_text("Available mods", size=12, color="black")
            available_label_photo = ImageTk.PhotoImage(available_label_img)
            canvas.create_image(225, 73, anchor=tk.W, image=available_label_photo)
            selection_dialog.available_label_photo = available_label_photo
            
            current_label_img = self.font_manager.render_text("Load order", size=12, color="black")
            current_label_photo = ImageTk.PhotoImage(current_label_img)
            canvas.create_image(500, 73, anchor=tk.E, image=current_label_photo)
            selection_dialog.current_label_photo = current_label_photo
            
            # LEFT SIDE - Available Mods Listbox (directly on canvas)
            available_listbox_frame = tk.Frame(canvas, bg=BG_COLOR)
            canvas.create_window(13, 70, anchor=tk.NW, window=available_listbox_frame, width=330, height=340)
            
            available_listbox = tk.Listbox(available_listbox_frame,
                                        height=12,
                                        bg="#a8a692",
                                        fg="black", 
                                        selectbackground="#4A4A4A",
                                        selectforeground="white",
                                        font=("Arial", 9),
                                        relief=tk.SUNKEN,
                                        borderwidth=2)
            available_scrollbar = tk.Scrollbar(available_listbox_frame, orient=tk.VERTICAL, command=available_listbox.yview)
            available_listbox.configure(yscrollcommand=available_scrollbar.set)
            
            available_listbox.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
            available_scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
            
            # RIGHT SIDE - Current Load Order Listbox (directly on canvas)
            current_listbox_frame = tk.Frame(canvas, bg=BG_COLOR)
            canvas.create_window(356, 70, anchor=tk.NW, window=current_listbox_frame, width=330, height=340)
            
            current_listbox = tk.Listbox(current_listbox_frame,
                                        height=12,
                                        bg="#a8a692",
                                        fg="black", 
                                        selectbackground="#4A4A4A",
                                        selectforeground="white",
                                        font=("Arial", 9),
                                        relief=tk.SUNKEN,
                                        borderwidth=2)
            current_scrollbar = tk.Scrollbar(current_listbox_frame, orient=tk.VERTICAL, command=current_listbox.yview)
            current_listbox.configure(yscrollcommand=current_scrollbar.set)
            
            current_listbox.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
            current_scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
                        
            # Add mods to available listbox - NO MAPPING NEEDED
            for mod_name, _ in available_mods_with_info:
                if mod_name not in self.state.mod_names:
                    available_listbox.insert(tk.END, mod_name)

            def add_mod():
                selection = available_listbox.curselection()
                if selection:
                    index = selection[0]
                    mod_name = available_listbox.get(index)  # Get directly from listbox
                    if mod_name:
                        current_listbox.insert(tk.END, mod_name)
                        available_listbox.delete(index)

            def remove_mod():
                selection = current_listbox.curselection()
                if selection:
                    index = selection[0]
                    mod_name = current_listbox.get(index)  # Get directly from listbox
                    current_listbox.delete(index)
                    available_listbox.insert(tk.END, mod_name)

            def move_up():
                selection = current_listbox.curselection()
                if selection and selection[0] > 0:
                    index = selection[0]
                    mod_name = current_listbox.get(index)
                    current_listbox.delete(index)
                    current_listbox.insert(index - 1, mod_name)
                    current_listbox.select_set(index - 1)

            def move_down():
                selection = current_listbox.curselection()
                if selection and selection[0] < current_listbox.size() - 1:
                    index = selection[0]
                    mod_name = current_listbox.get(index)
                    current_listbox.delete(index)
                    current_listbox.insert(index + 1, mod_name)
                    current_listbox.select_set(index + 1)

            def apply_selection():
                new_mods = []
                for i in range(current_listbox.size()):
                    mod_name = current_listbox.get(i)
                    new_mods.append(mod_name)
                    
                self.state.mod_names = ModManager.validate_mod_order(new_mods, self.state.vic2_path)
                self.state.config.default_mods = self.state.mod_names.copy()
                self.state.config.last_mods = self.state.mod_names.copy()
                self.state.config.save()
                
                # CLEAR ALL MOD-SPECIFIC CACHES
                self._clear_mod_caches()
                
                self._reload_assets()
                self._reload_cb_icons()
                mod_display = ', '.join(self.state.mod_names) if self.state.mod_names else 'None'
                self.mod_label.config(text=f"Current Mods: {mod_display}")
                selection_dialog.destroy()

            # BOTTOM BUTTONS - directly on canvas
            buttons_frame = tk.Frame(canvas, bg=BG_COLOR)
            canvas.create_window(350, 400, anchor=tk.CENTER, window=buttons_frame)
            button_images = []           
            def create_button_on_canvas(x, y, text, command, image_path, width, height, flip_horizontal=False):
                """Create a button directly on the canvas with proper text compositing."""
                btn_image = SafeLoader.safe_load_image(image_path, (width, height))
                
                if btn_image and flip_horizontal:
                    btn_image = btn_image.transpose(Image.FLIP_LEFT_RIGHT)
                
                if btn_image:
                    # Ensure both images are RGBA
                    if btn_image.mode != 'RGBA':
                        btn_image = btn_image.convert('RGBA')
                    
                    # Create the button image with text using alpha_composite (like the working version)
                    composite = btn_image.copy()
                    
                    # Add text to the button image using proper alpha compositing
                    text_img = self.font_manager.render_text(text, size=18, color="black")
                    if text_img:
                        # Crop transparent padding from text
                        bbox = text_img.getbbox()
                        if bbox:
                            text_img = text_img.crop(bbox)
                        
                        # Ensure text image is RGBA
                        if text_img.mode != 'RGBA':
                            text_img = text_img.convert('RGBA')
                        
                        # Calculate position to center text on button
                        text_x = (width - text_img.width) // 2
                        text_y = (height - text_img.height) // 2
                        
                        # Create a temporary image for proper compositing
                        temp_image = Image.new('RGBA', composite.size, (0, 0, 0, 0))
                        temp_image.paste(text_img, (text_x, text_y))
                        
                        # Use alpha_composite instead of paste to handle transparency properly
                        composite = Image.alpha_composite(composite, temp_image)
                    
                    btn_photo = ImageTk.PhotoImage(composite)
                    button_images.append(btn_photo)  # Store reference
                    
                    # Create the button as a canvas image item
                    btn_id = canvas.create_image(x, y, anchor=tk.CENTER, image=btn_photo)
                    
                    # Make it clickable
                    canvas.tag_bind(btn_id, "<Button-1>", lambda e: command())
                    canvas.tag_bind(btn_id, "<Enter>", lambda e: canvas.config(cursor="hand2"))
                    canvas.tag_bind(btn_id, "<Leave>", lambda e: canvas.config(cursor=""))
                    
                    return btn_id
                else:
                    # Fallback - create a simple text button on canvas
                    btn_id = canvas.create_rectangle(x-50, y-15, x+50, y+15, fill="#4A4A4A", outline="white")
                    text_id = canvas.create_text(x, y, text=text, fill="white", font=("Arial", 9))
                    
                    canvas.tag_bind(btn_id, "<Button-1>", lambda e: command())
                    canvas.tag_bind(text_id, "<Button-1>", lambda e: command())
                    canvas.tag_bind(btn_id, "<Enter>", lambda e: canvas.config(cursor="hand2"))
                    canvas.tag_bind(text_id, "<Enter>", lambda e: canvas.config(cursor="hand2"))
                    canvas.tag_bind(btn_id, "<Leave>", lambda e: canvas.config(cursor=""))
                    canvas.tag_bind(text_id, "<Leave>", lambda e: canvas.config(cursor=""))
                    
                    return btn_id
            
            # Create buttons directly on canvas
            button_y = 440  # Y position for all buttons
            
            add_btn = create_button_on_canvas(295, button_y, "Add", add_mod,
                                            self.state.get_modded_path(os.path.join("gfx", "interface", "unit_selectonly.dds")),
                                            108, 26, flip_horizontal=True)
            
            remove_btn = create_button_on_canvas(405, button_y, "Remove", remove_mod,
                                                self.state.get_modded_path(os.path.join("gfx", "interface", "unit_selectonly.dds")),
                                                108, 26, flip_horizontal=False)
            
            up_btn = create_button_on_canvas(515, button_y, "Move up", move_up,
                                            self.state.get_modded_path(os.path.join("gfx", "interface", "button_standard_111.tga")),
                                            111, 34, flip_horizontal=False)
            
            down_btn = create_button_on_canvas(630, button_y, "Move down", move_down,
                                            self.state.get_modded_path(os.path.join("gfx", "interface", "button_standard_111.tga")),
                                            111, 34, flip_horizontal=False)
            
            apply_btn = create_button_on_canvas(70, button_y, "Apply", apply_selection,
                                            self.state.get_modded_path(os.path.join("gfx", "interface", "button_standard_111.tga")),
                                            111, 34, flip_horizontal=False)
            
            # Store the button images to prevent garbage collection
            selection_dialog.button_images = button_images
        
        def clear_mods():
            self.state.mod_names = []
            self.state.config.default_mods = []
            self.state.config.last_mods = []
            self.state.config.save()
            
            # CLEAR ALL MOD-SPECIFIC CACHES
            self._clear_mod_caches()
            
            self._reload_assets()
            self._reload_cb_icons()
            self.mod_label.config(text="Current Mods: None")
        
        mod_btn = self._create_button("Select Mods", choose_mods)
        self.canvas.create_window(30, 70, anchor=tk.NW, window=mod_btn)
        
        clear_btn = self._create_button("Clear Mods", clear_mods)
        self.canvas.create_window(200, 70, anchor=tk.NW, window=clear_btn)
        
        # Update mod label to show multiple mods
        mod_display = ', '.join(self.state.mod_names) if self.state.mod_names else 'None'
        self.mod_label = tk.Label(self.root, text=f"Current Mods: {mod_display}", 
                                bg=BG_COLOR, font=("Arial", 10))
        self.canvas.create_window(30, 110, anchor=tk.NW, window=self.mod_label)
        
        # Custom checkboxes
        self.auto_load_checkbox = CustomCheckbox(
            self.canvas, 
            self.state, 
            'auto_load_last', 
            "Auto-load last save file", 
            350, 148
        )

        def on_auto_load_toggle(value):
            pass 

        self.auto_load_checkbox = CustomCheckbox(
            self.canvas, 
            self.state, 
            'auto_load_last', 
            "Auto-load last save file", 
            350, 148,
            callback=on_auto_load_toggle
        )
        
        # Save file selector
        def choose_save():
            file_path = filedialog.askopenfilename(
                title="Select Save File",
                filetypes=[("Victoria 2 Save Files", "*.v2"), ("All Files", "*.*")]
            )
            if file_path:
                self.state.save_file_path = file_path
                filename = os.path.basename(file_path)
                folder1 = os.path.basename(os.path.dirname(file_path))  # Immediate parent
                folder2 = os.path.basename(os.path.dirname(os.path.dirname(file_path)))  # Grandparent
                display_name = f"{folder2}/{folder1}/{filename}"
                self.save_label.config(text=display_name)
                
                # Add to recent files
                if file_path in self.state.config.recent_files:
                    self.state.config.recent_files.remove(file_path)
                self.state.config.recent_files.insert(0, file_path)
                self.state.config.recent_files = self.state.config.recent_files[:10]
                self.state.config.save()
                
                # Refresh recent files display
                self._refresh_recent_files()

        save_btn = self._create_button("Select Save File", choose_save)
        self.canvas.create_window(30, 140, anchor=tk.NW, window=save_btn)
        
        load_btn = self._create_button("Load Save File", self.load_save)
        self.canvas.create_window(200, 140, anchor=tk.NW, window=load_btn)
        
        self.save_label = tk.Label(self.root, text=self.state.save_file_path, wraplength=600, 
                                justify=tk.LEFT, bg=BG_COLOR, font=("Arial", 9))
        self.canvas.create_window(30, 180, anchor=tk.NW, window=self.save_label)
        
        # Recent files section
        recent_label = tk.Label(self.root, text="Recent Files:", bg=BG_COLOR, font=("Arial", 10, "bold"))
        self.canvas.create_window(30, 320, anchor=tk.NW, window=recent_label)
        
        # Display recent files
        recent_y = 350
        self.recent_file_buttons = []
        
        for i, file_path in enumerate(self.state.config.recent_files):
            if os.path.exists(file_path):
                filename = os.path.basename(file_path)
                btn = tk.Label(self.root, text=f"{i+1}. {filename}", 
                            cursor="hand2", bg=BG_COLOR, font=("Arial", 10), fg="blue")
                btn.bind("<Button-1>", lambda e, path=file_path: self._load_recent_file(path))
                self.canvas.create_window(30, recent_y, anchor=tk.NW, window=btn)
                self.recent_file_buttons.append(btn)
                recent_y += 25
    
    def _draw_wars_tab(self):
        """Draw the wars list tab with pagination."""
        self._rebuild_filtered_wars()
        
        # War list with pagination
        war_list = WarList(self.canvas, self.root, self.state, self.scrollbar_assets, 
                        self.font_manager, self.image_refs, self._on_tab_click, self)
        war_list.create(self.state.filtered_wars)
        
        # ADD SORT BUTTONS HERE
        self._draw_sort_buttons()
        
        # Country filter
        country_filter = CountryFilter(self.canvas, self.root, self.state, 
                            self.scrollbar_assets, self.image_refs, 
                            self._rebuild_and_refresh, self,
                            self.text_renderer) 
        country_filter.create()

    def _rebuild_and_refresh(self):
        """Rebuild filtered wars and refresh the display with pagination reset."""
        self._rebuild_filtered_wars()
        
        # Reset to first page when filters change
        if hasattr(self, 'war_list'):
            self.war_list.current_page = 0
            self.war_list.all_wars = self.state.filtered_wars
            self.war_list._load_current_page()
        else:
            self._draw_tab_content("Tab2")

    def _raise_tabs_to_top(self):
        """Force tab canvases to top z-order."""
        if hasattr(self, 'tab_canvases'):
            for canvas in self.tab_canvases:
                try:
                    # Get the window ID for this canvas
                    items = self.canvas.find_withtag(canvas)
                    for item in items:
                        self.canvas.tag_raise(item)
                except:
                    pass

    def _draw_clear_filter_button(self, button_file: str, x: int, y: int, width: int, height: int, text: str, crop_top: int, crop_bottom: int):
        """Draw the clear filter button with click functionality."""
        button_path = self.state.get_modded_path(
            os.path.join("gfx", "interface", button_file)
        )
        button_img = SafeLoader.safe_load_image(button_path)
        
        if button_img:
            if crop_top > 0 or crop_bottom > 0:
                # Crop the image
                cropped_img = ImageLoader.crop(button_img, top=crop_top, bottom=crop_bottom)
            else:
                # No cropping needed
                cropped_img = button_img
            
            # Resize to specified width and height
            final_img = cropped_img.resize((width, height), Image.Resampling.LANCZOS)
            
            # Create canvas for button with separate text rendering
            button_canvas = tk.Canvas(self.canvas, width=width, height=height, bg=BG_COLOR,
                                    borderwidth=0, highlightthickness=0)
            
            # Convert button image to PhotoImage
            btn_photo = ImageTk.PhotoImage(final_img)
            self.image_refs.append(btn_photo)
            button_canvas.create_image(0, 0, anchor=tk.NW, image=btn_photo)
            
            # Add text using BMFont
            if text:
                text_image = self.font_manager.render_text(text, size=18, color="black")
                
                # Crop transparent padding for proper centering
                bbox = text_image.getbbox()
                if bbox:
                    text_image = text_image.crop(bbox)
                
                # Convert text to PhotoImage
                text_photo = ImageTk.PhotoImage(text_image)
                self.image_refs.append(text_photo)
                
                # Calculate centered position for text
                text_x = width // 2
                text_y = height // 2
                
                # Add text as separate layer above button
                button_canvas.create_image(text_x, text_y, anchor=tk.CENTER, image=text_photo)
            
            # Place the button canvas on the main canvas
            button_id = self.canvas.create_window(x, y, anchor=tk.NW, window=button_canvas)
            
            # Bind click event to clear filter
            def clear_filter(event):
                self.state.selected_countries.clear()
                self._rebuild_and_refresh()
            
            button_canvas.bind("<Button-1>", clear_filter)
            button_canvas.configure(cursor="hand2")
            
        else:
            # Fallback: draw rectangle and text if image loading fails
            rect_id = self.canvas.create_rectangle(x, y, x + width, y + height, 
                                                outline="#888", fill="#ccc")
            # For fallback, use consistent positioning
            text_id = self.canvas.create_text(
                x + width // 2, 
                y + height // 2,
                text=text, 
                font=("Arial", 10), 
                fill="black",
                anchor="center"
            )
            
            # Bind click event to clear filter for fallback button
            def clear_filter(event):
                self.state.selected_countries.clear()
                self._rebuild_and_refresh()
            
            self.canvas.tag_bind(rect_id, "<Button-1>", clear_filter)
            self.canvas.tag_bind(text_id, "<Button-1>", clear_filter)

    def _draw_sort_buttons(self):
        """Draw all sort buttons above the country filter."""
        # Calculate war count for display
        # war_count = len(self.state.filtered_wars)
        # total_wars = len(self.state.wars_data)
        # war_count_text = f"Wars: {war_count}/{total_wars}"  # Shorter format
        buttons = [
            ("sortbutton_164.dds", SORT_BUTTON_X, SORT_BUTTON_Y, 166, 20, "Country", 5, 7),
            ("sortbutton_42.dds", SORT_BUTTON_X + 166, SORT_BUTTON_Y, 42, 20, "", 5, 7),
            ("sortbutton_200.dds", SORT_BUTTON_X + 166 + 42, SORT_BUTTON_Y, 200, 20, "Clear filter", 5, 7),
            ("sortbutton_soi.dds", SORT_BUTTON_X + 174 + 42 + 24 * 8, SORT_BUTTON_Y, 28, 20, "", 0, 0),
            ("sort_prestige_rank.dds", SORT_BUTTON_X + 174 + 42 + 24 * 8 + 28, SORT_BUTTON_Y, 24, 20, "", 2, 2),
            ("sort_industry_rank.dds", SORT_BUTTON_X + 174 + 42 + 24 * 9 + 26, SORT_BUTTON_Y, 24, 20, "", 2, 2),
            ("sort_military_rank.dds", SORT_BUTTON_X + 174 + 42 + 24 * 10 + 24, SORT_BUTTON_Y, 24, 20, "", 2, 2),
            ("sortbutton_totalrank.dds", SORT_BUTTON_X + 174 + 42 + 24 * 11 + 22, SORT_BUTTON_Y, 24, 20, "", 0, 0),
            ("sortbutton_68.dds", SORT_BUTTON_X + 174 + 42 + 24 * 12 + 20, SORT_BUTTON_Y, 68, 20, "", 5, 7),
            ("sortbutton_relation.dds", SORT_BUTTON_X + 174 + 42 + 24 * 12 + 20 + 68, SORT_BUTTON_Y, 28, 20, "", 0, 0),
        ]
        
        for i, (button_file, x, y, width, height, text, crop_top, crop_bottom) in enumerate(buttons):
            if text == "Clear filter":
                # Create the clear filter button with click handler
                self._draw_clear_filter_button(button_file, x, y, width, height, text, crop_top, crop_bottom)
            else:
                self._draw_single_sort_button(button_file, x, y, width, height, text, crop_top, crop_bottom)


    def _draw_battle_sort_buttons(self):
        """Draw sort buttons above the battle list in war details tab."""
        # Define battle-specific sort buttons
        battle_buttons = [
            ("sortbutton_164.dds", SORT_BUTTON_X, SORT_BUTTON_Y, 166, 20, "Province", 5, 7),
            ("sortbutton_42.dds", SORT_BUTTON_X + 166, SORT_BUTTON_Y, 42, 20, "", 5, 7),
            ("sortbutton_200.dds", SORT_BUTTON_X + 166 + 42, SORT_BUTTON_Y, 200, 20, "", 5, 7),
            ("sortbutton_soi.dds", SORT_BUTTON_X + 174 + 42 + 24 * 8, SORT_BUTTON_Y, 28, 20, "", 0, 0),
            ("sort_prestige_rank.dds", SORT_BUTTON_X + 174 + 42 + 24 * 8 + 28, SORT_BUTTON_Y, 24, 20, "", 2, 2),
            ("sort_industry_rank.dds", SORT_BUTTON_X + 174 + 42 + 24 * 9 + 26, SORT_BUTTON_Y, 24, 20, "", 2, 2),
            ("sort_military_rank.dds", SORT_BUTTON_X + 174 + 42 + 24 * 10 + 24, SORT_BUTTON_Y, 24, 20, "", 2, 2),
            ("sortbutton_totalrank.dds", SORT_BUTTON_X + 174 + 42 + 24 * 11 + 22, SORT_BUTTON_Y, 24, 20, "", 0, 0),
            ("sortbutton_68.dds", SORT_BUTTON_X + 174 + 42 + 24 * 12 + 20, SORT_BUTTON_Y, 68, 20, "", 5, 7),
            ("sortbutton_relation.dds", SORT_BUTTON_X + 174 + 42 + 24 * 12 + 20 + 68, SORT_BUTTON_Y, 28, 20, "", 0, 0),
        ]
        
        for button_file, x, y, width, height, text, crop_top, crop_bottom in battle_buttons:
            self._draw_single_sort_button(button_file, x, y, width, height, text, crop_top, crop_bottom)

    def _draw_single_sort_button(self, button_file: str, x: int, y: int, width: int, height: int, text: str, crop_top: int, crop_bottom: int):
        """Draw a single sort button with the specified parameters."""
        button_path = self.state.get_modded_path(
            os.path.join("gfx", "interface", button_file)
        )
        button_img = SafeLoader.safe_load_image(button_path)
        
        if button_img:
            if crop_top > 0 or crop_bottom > 0:
                # Crop the image
                cropped_img = ImageLoader.crop(button_img, top=crop_top, bottom=crop_bottom)
            else:
                # No cropping needed
                cropped_img = button_img
            
            # Resize to specified width and height
            final_img = cropped_img.resize((width, height), Image.Resampling.LANCZOS)
            
            # Create canvas for button with separate text rendering
            button_canvas = tk.Canvas(self.canvas, width=width, height=height, bg=BG_COLOR,
                                    borderwidth=0, highlightthickness=0)
            
            # Convert button image to PhotoImage
            btn_photo = ImageTk.PhotoImage(final_img)
            self.image_refs.append(btn_photo)
            button_canvas.create_image(0, 0, anchor=tk.NW, image=btn_photo)
            
            # Add text using BMFont
            if text:
                text_image = self.font_manager.render_text(text, size=18, color="black")
                
                # Crop transparent padding for proper centering
                bbox = text_image.getbbox()
                if bbox:
                    text_image = text_image.crop(bbox)
                
                # Convert text to PhotoImage
                text_photo = ImageTk.PhotoImage(text_image)
                self.image_refs.append(text_photo)
                
                # Calculate centered position for text
                text_x = width // 2
                text_y = height // 2
                
                # Add text as separate layer above button
                button_canvas.create_image(text_x, text_y, anchor=tk.CENTER, image=text_photo)
            
            # Place the button canvas on the main canvas
            button_id = self.canvas.create_window(x, y, anchor=tk.NW, window=button_canvas)
            
            # ADD CLICK HANDLER FOR CLEAR FILTER BUTTON
            if text == "Clear filter":
                def clear_filter(event):
                    self.state.selected_countries.clear()
                    self._rebuild_and_refresh()
                    # After clearing filter, redraw the tab to update the war count
                    self._draw_tab_content("Tab2")
                button_canvas.bind("<Button-1>", clear_filter)
                button_canvas.configure(cursor="hand2")
            
        else:
            # Fallback: draw rectangle and text if image loading fails
            rect_id = self.canvas.create_rectangle(x, y, x + width, y + height, 
                                                outline="#888", fill="#ccc")
            if text:
                # For fallback, use consistent positioning
                text_id = self.canvas.create_text(
                    x + width // 2, 
                    y + height // 2,
                    text=text, 
                    font=("Arial", 10), 
                    fill="black",
                    anchor="center"
                )
                
                # ADD CLICK HANDLER FOR CLEAR FILTER BUTTON - UPDATED
                if text == "Clear filter":
                    def clear_filter(event):
                        self.state.selected_countries.clear()
                        self._rebuild_filtered_wars()  # Just update the filtered list
                        self._draw_tab_content("Tab2")  # Redraw the tab to refresh display
                    self.canvas.tag_bind(rect_id, "<Button-1>", clear_filter)
                    self.canvas.tag_bind(text_id, "<Button-1>", clear_filter)
        
    def _get_all_filter_countries(self) -> List[str]:
        """Get all countries that should appear in the filter list."""
        all_tags = set()
        for war in self.state.wars_data:
            all_tags.update(war.attackers)
            all_tags.update(war.defenders)
        return sorted([t for t in all_tags if t and t != "---"])
    
    def _rebuild_filtered_wars(self):
        """Rebuild the filtered wars list based on selected countries - uses cached data."""
        if not self.state.selected_countries:
            self.state.filtered_wars = self.state._wars_data[:] if self.state._wars_data else []
            return
        
        filtered = []
        for war in self.state._wars_data:
            attackers = set(war.attackers)
            defenders = set(war.defenders)
            if attackers.intersection(self.state.selected_countries) or \
            defenders.intersection(self.state.selected_countries):
                filtered.append(war)
        
        self.state.filtered_wars = filtered
    
    def _rebuild_and_refresh(self):
        """Rebuild filtered wars and refresh the display."""
        self._rebuild_filtered_wars()
        self._draw_tab_content("Tab2")
    
    def _reload_assets(self):
        """Reload all assets after mod change with debug info."""        
        # Clear image cache
        self.state.image_cache.clear()

        # Clear all file parsing caches
        self.state._localization_cache = None
        self.state._government_cache_loaded = False
        self.state._culture_mappings_cache = None
        self.state._cb_types_cache = None
        self.state._unit_types_cache = None
        
        self._load_government_data()
        
        self.layer_cache.build(self.tab_definitions)
        
        self._draw_tab_content(self.current_tab)

    def _reload_cb_icons(self):
        """Reload CB icons with debug info."""      
        # Clear the cache to force reload
        if hasattr(self, '_cb_icons_loaded'):
            self._cb_icons_loaded = False
        if hasattr(self, '_cb_icons'):
            self._cb_icons.clear()
        if hasattr(self, 'cb_to_sprite'):
            self.cb_to_sprite.clear()
                # Reload the icons
    
        # Reload the icons
        self._load_cb_icons()

    def _create_status_bar(self):
        """Create a status bar at the bottom of the window."""
        self.status_var = tk.StringVar()
        self.status_var.set("Ready")
        
        status_bar = tk.Label(self.root, textvariable=self.status_var, relief=tk.SUNKEN, 
                            anchor=tk.W, bg=BG_COLOR, font=("Arial", 9))
        status_bar.pack(side=tk.BOTTOM, fill=tk.X)
        
        # Store reference
        self.status_bar = status_bar

    def _set_status(self, message):
        """Update the status bar message."""
        if hasattr(self, 'status_var'):
            self.status_var.set(message)
            self.root.update_idletasks()

    def _clear_status(self):
        """Clear the status bar."""
        if hasattr(self, 'status_var'):
            self.status_var.set("Ready")
    
    def _on_tab_click(self, tab_name: str):
        """Handle tab click with smart memory management."""
        # If going FROM war details tab, clean war-specific images
        if self.current_tab == "Tab3":
            self.emergency_cleanup()  # Only clears war images now
        
        self.current_tab = tab_name
        self._set_status(f"Loading {self.tab_definitions[tab_name]['label']}...")
        self._draw_tab_content(tab_name)
        self._clear_status()
    
    def cleanup(self):
        """Clean up resources before closing."""
        try:
            self.state.config.save()
            self.state.image_cache.clear()
            self.layer_cache.clear()
            self.image_refs.clear()
            self.root.destroy()
        except Exception:
            pass


import weakref

class WarList:
    def __init__(self, parent_canvas, root, state, scrollbar_assets, font_manager, image_refs, on_war_select, gui=None):
        self.parent_canvas = parent_canvas
        self.root = root
        self.state = state
        self.sb_assets = scrollbar_assets
        self.font_manager = font_manager
        self.main_image_refs = image_refs
        self.on_war_select = on_war_select
        self.gui = gui
        
        # Pagination variables
        self.current_page = 0
        self.wars_per_page = 200
        self.all_wars = []
        self.scrollable = None
        self.navigation_frame = None
        self.flag_photo_cache = {}
        self.current_page_image_refs = []  # Track current page images
        self.current_canvas_items = []     # Track canvas items to delete

    def _load_current_page(self):
        """Load and display the current page of wars with proper image cleanup."""
        if not self.scrollable or not self.scrollable.frame_inside:
            return
            
        # Clear the current display
        for widget in self.scrollable.frame_inside.winfo_children():
            widget.destroy()
        
        # PROPERLY DESTROY PREVIOUS PAGE'S IMAGES
        for img_ref in self.current_page_image_refs:
            if img_ref in self.main_image_refs:
                self.main_image_refs.remove(img_ref)
            # PROPER WAY: Let garbage collection handle it after removing references
            # Just remove the reference and let GC do its job
        
        self.current_page_image_refs.clear()
        self.flag_photo_cache.clear()
        self.state.war_image_cache.clear()
        
        # Force garbage collection MULTIPLE TIMES
        import gc
        for i in range(3):  # Multiple passes
            gc.collect()
            self.root.update()  # Give tkinter time to process
        
        # Calculate range for current page
        start_idx = self.current_page * self.wars_per_page
        end_idx = min(start_idx + self.wars_per_page, len(self.all_wars))
        current_wars = self.all_wars[start_idx:end_idx]
        
        # Load assets for current page only
        overlay_path = self.state.get_modded_path(os.path.join("gfx", "interface", "flag_overlay_new.tga"))
        flag_overlay = SafeLoader.safe_load_image(overlay_path, size=(FLAG_WIDTH, FLAG_HEIGHT))
        
        bg_path = self.state.get_modded_path(os.path.join("gfx", "interface", "diplo_war_entrybg.dds"))
        entry_bg = SafeLoader.safe_load_image(bg_path)
        
        # Store references to assets for this page
        if flag_overlay:
            flag_photo = ImageTk.PhotoImage(flag_overlay)
            self.main_image_refs.append(flag_photo)
            self.current_page_image_refs.append(flag_photo)
        
        if entry_bg:
            entry_photo = ImageTk.PhotoImage(entry_bg)
            self.main_image_refs.append(entry_photo)
            self.current_page_image_refs.append(entry_photo)
        
        # Render current page wars WITH ALL GRAPHICS
        for idx, war in enumerate(current_wars):
            absolute_idx = start_idx + idx
            self._draw_war_row(self.scrollable.frame_inside, war, absolute_idx, flag_overlay, entry_bg)
        
        # Update navigation
        self._update_navigation_display()

    def create(self, wars: List[War]):
        """Create the war list with pagination controls."""
        self.all_wars = wars
        
        # Create scrollable area
        self.scrollable = ScrollableList(self.parent_canvas, self.root, self.state, WAR_LIST_X, WAR_LIST_Y, 
                                WAR_LIST_WIDTH, WAR_LIST_HEIGHT, self.sb_assets, row_height=56)
        self.scrollable.create()
        
        # Create navigation controls
        self._create_navigation_controls()
        
        # Load first page
        self._load_current_page()

    def _create_navigation_controls(self):
        """Create pagination navigation buttons below the war list."""
        # Position navigation below the war list
        nav_y = WAR_LIST_Y + WAR_LIST_HEIGHT
        
        # Create frame for navigation with EXACT same width as war list
        self.navigation_frame = tk.Frame(self.root, bg=BG_COLOR)
        
        # Use create_rectangle to force the exact width and position
        frame_id = self.parent_canvas.create_window(
            WAR_LIST_X, 
            nav_y, 
            anchor=tk.NW, 
            window=self.navigation_frame,
            width=600,  # FORCE the exact width to match war list
            height=16   # Set a fixed height
        )
        
        # Update navigation display
        self._update_navigation_display()

    def _update_navigation_display(self):
        """Update the navigation controls with properly cropped unitstatus_moving.dds arrows."""
        if not self.navigation_frame:
            return
            
        # Clear existing widgets
        for widget in self.navigation_frame.winfo_children():
            widget.destroy()
        
        total_pages = (len(self.all_wars) + self.wars_per_page - 1) // self.wars_per_page
        current_start = self.current_page * self.wars_per_page + 1
        current_end = min((self.current_page + 1) * self.wars_per_page, len(self.all_wars))
        
        # Create a canvas that fills the entire navigation frame width
        nav_canvas = tk.Canvas(self.navigation_frame, width=620, height=16, bg=BG_COLOR,
                            highlightthickness=0, borderwidth=0)
        nav_canvas.pack(fill=tk.BOTH, expand=True)
        
        # Left: Wars count
        wars_count_text = f"Wars {current_start}-{current_end} of {len(self.all_wars)}"
        nav_canvas.create_text(10, 6, text=wars_count_text, anchor=tk.W, 
                            font=("Arial", 9), fill="black")
        
        # Center: Page navigation with arrow buttons
        center_x = 300
        
        # Load and crop the arrow image
        arrow_path = self.state.get_modded_path(os.path.join("gfx", "interface", "unitstatus_moving.dds"))
        arrow_img = SafeLoader.safe_load_image(arrow_path)
        
        if arrow_img:
            # Crop to 16x11: 3 from left, 5 from right, 6 from top, 7 from bottom
            # Original 24x24 -> crop(3, 6, 24-5, 24-7) = crop(3, 6, 19, 17)
            cropped_arrow = arrow_img.crop((3, 6, 19, 17))  # 16x11 pixels
            
            # Page indicator  
            page_text = f"Page {self.current_page + 1}/{total_pages}"
            page_label = tk.Label(nav_canvas, text=page_text, bg=BG_COLOR, font=("Arial", 9))
            nav_canvas.create_window(center_x, 6, window=page_label)
            
            # Left arrow button - only show if there's a previous page
            if self.current_page > 0:
                # Use original cropped arrow pointing left
                left_arrow_photo = ImageTk.PhotoImage(cropped_arrow)
                self.main_image_refs.append(left_arrow_photo)
                self.current_page_image_refs.append(left_arrow_photo)
                
                left_btn = nav_canvas.create_image(center_x - 40, 6, image=left_arrow_photo)
                nav_canvas.tag_bind(left_btn, "<Button-1>", lambda e: self._load_previous_page())
                nav_canvas.tag_bind(left_btn, "<Enter>", lambda e: nav_canvas.config(cursor="hand2"))
                nav_canvas.tag_bind(left_btn, "<Leave>", lambda e: nav_canvas.config(cursor=""))
            
            # Right arrow button - only show if there's a next page
            if self.current_page < total_pages - 1:
                # Flip horizontally for right arrow
                right_arrow_img = cropped_arrow.transpose(Image.FLIP_LEFT_RIGHT)
                right_arrow_photo = ImageTk.PhotoImage(right_arrow_img)
                self.main_image_refs.append(right_arrow_photo)
                self.current_page_image_refs.append(right_arrow_photo)
                
                right_btn = nav_canvas.create_image(center_x + 40, 5, image=right_arrow_photo)
                nav_canvas.tag_bind(right_btn, "<Button-1>", lambda e: self._load_next_page())
                nav_canvas.tag_bind(right_btn, "<Enter>", lambda e: nav_canvas.config(cursor="hand2"))
                nav_canvas.tag_bind(right_btn, "<Leave>", lambda e: nav_canvas.config(cursor=""))

    def _preload_page_flags(self, wars: List[War]):
        """Preload flags for the current page only."""
        common_tags = set()
        for war in wars:
            # Only preload flags for first few participants to avoid excessive loading
            common_tags.update(war.attackers[:2])
            common_tags.update(war.defenders[:2])
        
        # for tag in common_tags:
        #     if tag and tag != "---":
        #         self._load_country_flag_for_display(tag)

    def _load_previous_page(self):
        """Load the previous page of wars."""
        if self.current_page > 0:
            self.current_page -= 1
            self._load_current_page()

    def _load_next_page(self):
        """Load the next page of wars."""
        total_pages = (len(self.all_wars) + self.wars_per_page - 1) // self.wars_per_page
        if self.current_page < total_pages - 1:
            self.current_page += 1
            self._load_current_page()

    def _determine_background_crop(self, war: War) -> str:
        """Determine which part of the background to crop based on player involvement and war status."""
        # Check if this is an active war
        is_active_war = self._is_active_war(war)
        
        # Check if player country is involved in this war
        player_involved = self._is_player_involved(war)
        
        # Determine crop position
        if player_involved:
            return "center"  # Player is involved - use center crop
        elif is_active_war:
            return "right"   # Active war but player not involved - use right crop
        else:
            return "left"    # Inactive war, player not involved - use left crop

    def _is_active_war(self, war: War) -> bool:
        """Determine if this is an active war."""
        return war.is_active

    def _is_player_involved(self, war: War) -> bool:
        """Check if the player's country is involved in this war."""
        # Get player country tag from save file
        player_tag = self._get_player_country_tag()
        
        if not player_tag:
            return False
        
        # Check if player is in attackers or defenders
        return player_tag in war.attackers or player_tag in war.defenders

    def _get_player_country_tag(self) -> Optional[str]:
        """Extract player country tag from save file text."""
        if not hasattr(self.state, 'save_file_text') or not self.state.save_file_text:
            return None
        
        # Look for the pattern: player="TAG" (usually on the second line)
        player_match = re.search(r'player\s*=\s*"([A-Z]{3})"', self.state.save_file_text)
        
        if player_match:
            return player_match.group(1)
        
        return None

    def _draw_war_row(self, parent, war: War, index: int, flag_overlay: Optional[Image.Image], entry_bg: Optional[Image.Image]):
        """Draw a single war row and store reference."""
        row = tk.Frame(parent, bg=BG_COLOR, height=56)
        row.pack(fill="x", pady=0)
        
        # Store the row frame so we can remove it later
        war.row_frame = row
        
        # Create canvas with transparent background
        row_canvas = tk.Canvas(row, width=600, height=56, highlightthickness=0, bg=BG_COLOR)
        row_canvas.pack(side="left", anchor="w")
        
        # Determine crop position based on player involvement and war status
        crop_position = self._determine_background_crop(war)
        
        # Background - create with dynamic cropping
        if entry_bg:
            # Convert background to have transparency
            bg_img = entry_bg
            
            # Simple cropping logic for 1800px wide image
            if crop_position == "center":
                # Crop 600px from center (600-1200 of 1800px)
                row_bg = bg_img.crop((600, 0, 1200, bg_img.height))
            elif crop_position == "right":
                # Crop 600px from right (1200-1800 of 1800px)
                row_bg = bg_img.crop((1200, 0, 1800, bg_img.height))
            else:  # left (default)
                # Crop 600px from left (0-600 of 1800px)
                row_bg = bg_img.crop((0, 0, 600, bg_img.height))
            
            # Ensure the background image has alpha channel
            if row_bg.mode != 'RGBA':
                row_bg = row_bg.convert('RGBA')
            
            bg_small = row_bg.resize((600, 56), Image.Resampling.NEAREST)
            photo = ImageTk.PhotoImage(bg_small)
            self.main_image_refs.append(photo)  # CHANGED: main_image_refs instead of image_refs
            self.current_page_image_refs.append(photo)  # TRACK FOR CLEANUP
            row_canvas.create_image(0, 0, anchor="nw", image=photo)
        
        # Title (already transparent)
        self._draw_war_title(row_canvas, war.name)
        
        # Calculate war statistics
        stats = WarAnalyzer.calculate_war_statistics(war)
        
        # Calculate total army size for both sides using commander-based method
        if not hasattr(self.gui, '_unit_types'):
            self.gui._unit_types = UnitTypeClassifier.load_unit_types(self.state)
        
        # Calculate using new commander-based method
        attacker_land, attacker_naval, defender_land, defender_naval, _, _ = self.gui.calculate_total_army_size_by_country(war, self.gui._unit_types)
        
        total_attacker_army = attacker_land + attacker_naval
        total_defender_army = defender_land + defender_naval
        
        # Format army sizes to be compact
        def format_army_size(num):
            if num >= 1000000:
                return f"{num/1000000:.1f}M"
            elif num >= 1000:
                return f"{num/1000:.0f}K"
            else:
                return str(num)

        attacker_army_str = format_army_size(total_attacker_army)
        defender_army_str = format_army_size(total_defender_army)

        # Draw attacker army size (left of war score) using normal text
        row_canvas.create_text(170, 40, text=attacker_army_str, anchor=tk.CENTER, 
                            font=("Arial", 8), fill="black")

        # Draw defender army size (right of war score) using normal text  
        row_canvas.create_text(429, 40, text=defender_army_str, anchor=tk.CENTER,
                            font=("Arial", 8), fill="black")
        
        # Draw war score visualization in center (original size with overlay)
        war_score = stats.war_score_estimate
        self.gui._draw_war_score(row_canvas, war_score, 300, 40)
        
        # Flags - use the original method
        self._draw_war_list_flags(row_canvas, war.attackers, 10, 8, "left", war.original_attacker, flag_overlay)
        self._draw_war_list_flags(row_canvas, war.defenders, 590, 8, "right", war.original_defender, flag_overlay)
        
        # Draw CB icon - RESTORE ORIGINAL SIZE
        if war.goals and self.gui:
            first_goal = war.goals[0]
            cb_name = first_goal.casus_belli
            
            if cb_name:
                cb_name_for_icon = cb_name.replace('_small', '')
                cb_icon = self.gui._get_cb_icon(cb_name_for_icon)
                if cb_icon:
                    # Ensure proper transparency
                    if cb_icon.mode != 'RGBA':
                        cb_icon = cb_icon.convert('RGBA')
                    
                    # RESTORE ORIGINAL SIZE (24x24) for war list too
                    cb_icon = cb_icon.resize((24, 24), Image.Resampling.LANCZOS)
                    
                    photo_icon = ImageTk.PhotoImage(cb_icon)
                    self.main_image_refs.append(photo_icon)  # CHANGED: main_image_refs instead of image_refs
                    self.current_page_image_refs.append(photo_icon)  # TRACK FOR CLEANUP
                    
                    # Position below the first attacker flag
                    row_canvas.create_image(19, 40, anchor=tk.CENTER, image=photo_icon)
        
        def on_click(e):
            self.state.selected_war_index = index
            self.on_war_select("Tab3")
        
        row.bind("<Button-1>", on_click)
        row_canvas.bind("<Button-1>", on_click)

    def _draw_war_title(self, canvas, title: str):
        """Draw war title on canvas - shortened to make room for stats."""
        # Shorten title if too long to fit with new stats
        if len(title) > 50:
            display_title = title[:48] + "..."
        else:
            display_title = title
        
        # Render text using BMFont
        text_image = self.font_manager.render_bold_text(display_title, size=18, color="white")
        
        # Crop transparent padding for proper centering
        bbox = text_image.getbbox()
        if bbox:
            text_image = text_image.crop(bbox)
        
        # Convert text to PhotoImage
        text_photo = ImageTk.PhotoImage(text_image)
        self.main_image_refs.append(text_photo)
        
        # Calculate centered position for text
        text_x = 100 + (400 // 2)  # 100 (left offset) + half of 400 width
        text_y = 17  # Half of 28 height
        
        # Add text directly to the canvas
        canvas.create_image(text_x, text_y, anchor=tk.CENTER, image=text_photo)

    def _draw_war_list_flags(self, canvas, tags: List[str], x_anchor: int, y: int, side: str, highlight: Optional[str], flag_overlay: Optional[Image.Image]):
        """Draw flags for war list with maximum width constraint."""
        tags = [t for t in tags if t and t != "---"]
        if not tags:
            return
        
        ordered = list(tags)
        if highlight in ordered:
            ordered.remove(highlight)
            ordered.insert(0, highlight)
        
        # Maximum width for the flag row (similar to war detail tab)
        max_total_width = 120  # Adjust this value as needed
        
        num_tags = len(ordered)
        if num_tags <= 5:
            step = 25
        elif num_tags <= 8:
            step = 8
        else:
            step = 4
        
        # Calculate total width needed
        total_width_needed = (num_tags - 1) * step + FLAG_WIDTH
        
        # If total width exceeds maximum, reduce spacing
        if total_width_needed > max_total_width:
            # Calculate new step to fit within max width
            step = (max_total_width - FLAG_WIDTH) / (num_tags - 1)
            step = max(8, step)  # Minimum spacing of 8 pixels
        
        total_width = (num_tags - 1) * step + FLAG_WIDTH
        x_start = x_anchor if side == "left" else (x_anchor - total_width)
        
        positions = [(tag, x_start + i * step, y) for i, tag in enumerate(ordered)]
        
        # Draw all flags except highlighted one first
        for tag, x, yy in [p for p in positions if p[0] != highlight]:
            self._draw_single_war_list_flag(canvas, tag, x, yy, flag_overlay)
        
        # Draw highlighted flag last (so it appears on top)
        if highlight and any(tag == highlight for tag, _, _ in positions):
            tag, x, yy = next(p for p in positions if p[0] == highlight)
            self._draw_single_war_list_flag(canvas, tag, x, yy, flag_overlay)

    def _draw_war_list_flags_advanced(self, canvas, tags: List[str], x_anchor: int, y: int, side: str, highlight: Optional[str], flag_overlay: Optional[Image.Image]):
        """Draw flags for war list with maximum width and dynamic spacing, with proper z-order."""
        tags = [t for t in tags if t and t != "---"]
        if not tags:
            return
        
        ordered = list(tags)
        if highlight in ordered:
            ordered.remove(highlight)
            ordered.insert(0, highlight)
        
        # Configuration
        max_total_width = 120
        min_spacing = 4
        max_spacing = 25
        preferred_spacing = 25
        
        num_tags = len(ordered)
        
        # Calculate optimal spacing
        if num_tags == 1:
            actual_spacing = 0
        else:
            total_width_needed = num_tags * preferred_spacing
            if total_width_needed <= max_total_width:
                actual_spacing = preferred_spacing
            else:
                actual_spacing = (max_total_width - FLAG_WIDTH) / (num_tags - 1)
                actual_spacing = max(min_spacing, min(max_spacing, actual_spacing))
        
        total_width = (num_tags - 1) * actual_spacing + FLAG_WIDTH
        x_start = x_anchor if side == "left" else (x_anchor - total_width)
        
        positions = [(tag, x_start + i * actual_spacing, y) for i, tag in enumerate(ordered)]
        
        # REVERSED: Draw from right to left so leftmost flags appear on top
        # First draw all non-highlighted flags from right to left
        non_highlighted = [p for p in positions if p[0] != highlight]
        for tag, x, yy in reversed(non_highlighted):  # REVERSED iteration
            self._draw_single_war_list_flag(canvas, tag, x, yy, flag_overlay)
        
        # Draw highlighted flag last (so it appears on top of everything)
        if highlight and any(tag == highlight for tag, _, _ in positions):
            tag, x, yy = next(p for p in positions if p[0] == highlight)
            self._draw_single_war_list_flag(canvas, tag, x, yy, flag_overlay)

    def _draw_single_war_list_flag(self, canvas, tag: str, x: int, y: int, overlay: Optional[Image.Image]):
        """Draw a single country flag WITHOUT CACHE to prevent memory leaks."""
        flag = self._load_flag(tag)
        if not flag:
            return
        
        flag = flag.resize((FLAG_WIDTH, FLAG_HEIGHT), Image.Resampling.LANCZOS)
        
        # Create composite with transparent background
        composite = Image.new("RGBA", (FLAG_WIDTH, FLAG_HEIGHT), (0, 0, 0, 0))
        composite.paste(flag, (0, 0))
        
        if overlay:
            composite.alpha_composite(overlay)
        
        # DON'T CACHE - create new PhotoImage every time
        photo = ImageTk.PhotoImage(composite)
        self.main_image_refs.append(photo)
        self.current_page_image_refs.append(photo)
        canvas.create_image(x, y, anchor="nw", image=photo)

    def _draw_single_flag(self, canvas, tag: str, x: int, y: int, overlay: Optional[Image.Image]):
        """Draw a single country flag."""
        flag = self._load_flag(tag)
        if not flag:
            return
        
        flag = flag.resize((FLAG_WIDTH, FLAG_HEIGHT), Image.Resampling.LANCZOS)
        
        if overlay:
            flag = flag.copy()
            flag.alpha_composite(overlay)
        
        photo = ImageTk.PhotoImage(flag)
        self.main_image_refs.append(photo)  # CHANGED: main_image_refs instead of image_refs
        self.current_page_image_refs.append(photo)  # TRACK FOR CLEANUP
        canvas.create_image(x, y, anchor="nw", image=photo)

    def _draw_flags(self, canvas, tags: List[str], x_anchor: int, y: int, side: str, highlight: Optional[str], flag_overlay: Optional[Image.Image]):
        """Draw flags for a list of country tags."""
        tags = [t for t in tags if t and t != "---"]
        if not tags:
            return
        
        ordered = list(tags)
        if highlight in ordered:
            ordered.remove(highlight)
            ordered.insert(0, highlight)
        
        num_tags = len(ordered)
        if num_tags <= 5:
            step = 24
        elif num_tags <= 9:
            step = 14
        else:
            step = 12
        
        total_width = (num_tags - 1) * step + FLAG_WIDTH
        x_start = x_anchor if side == "left" else (x_anchor - total_width)
        
        positions = [(tag, x_start + i * step, y) for i, tag in enumerate(ordered)]
        
        for tag, x, yy in [p for p in positions if p[0] != highlight]:
            self._draw_single_flag(canvas, tag, x, yy, flag_overlay)
        
        if highlight and any(tag == highlight for tag, _, _ in positions):
            tag, x, yy = next(p for p in positions if p[0] == highlight)
            self._draw_single_flag(canvas, tag, x, yy, flag_overlay)


    def _load_flag(self, tag: str) -> Optional[Image.Image]:
        """Load flag image for a country tag."""
        if not tag or tag == "---":
            return None
        
        cache_key = f"flag_{tag}"
        cached_flag = self.state.image_cache.get(cache_key)
        if cached_flag:
            return cached_flag
        
        gov_name = self.state.country_governments.get(tag)
        if gov_name:
            flag_type = self.state.gov_to_flagtype.get(gov_name)
            if flag_type:
                variant_path = self.state.get_modded_path(os.path.join("gfx", "flags", f"{tag}_{flag_type}.tga"))
                flag = SafeLoader.safe_load_image(variant_path)
                if flag:
                    self.state.image_cache.set(cache_key, flag)
                    return flag
        
        base_path = self.state.get_modded_path(os.path.join("gfx", "flags", f"{tag}.tga"))
        flag = SafeLoader.safe_load_image(base_path)
        if flag:
            self.state.image_cache.set(cache_key, flag)
        return flag
    
    def _draw_flags(self, canvas, tags: List[str], x_anchor: int, y: int, side: str, highlight: Optional[str], flag_overlay: Optional[Image.Image]):
        """Draw flags for a list of country tags."""
        tags = [t for t in tags if t and t != "---"]
        if not tags:
            return
        
        ordered = list(tags)
        if highlight in ordered:
            ordered.remove(highlight)
            ordered.insert(0, highlight)
        
        num_tags = len(ordered)
        if num_tags <= 5:
            step = 24
        elif num_tags <= 9:
            step = 14
        else:
            step = 12
        
        total_width = (num_tags - 1) * step + FLAG_WIDTH
        x_start = x_anchor if side == "left" else (x_anchor - total_width)
        
        positions = [(tag, x_start + i * step, y) for i, tag in enumerate(ordered)]
        
        for tag, x, yy in [p for p in positions if p[0] != highlight]:
            self._draw_single_flag(canvas, tag, x, yy, flag_overlay)
        
        if highlight and any(tag == highlight for tag, _, _ in positions):
            tag, x, yy = next(p for p in positions if p[0] == highlight)
            self._draw_single_flag(canvas, tag, x, yy, flag_overlay)
    
    
    def _load_flag(self, tag: str) -> Optional[Image.Image]:
        """Load flag image for a country tag."""
        if not tag or tag == "---":
            return None
        
        cache_key = f"flag_{tag}"
        cached_flag = self.state.image_cache.get(cache_key)
        if cached_flag:
            return cached_flag
        
        gov_name = self.state.country_governments.get(tag)
        if gov_name:
            flag_type = self.state.gov_to_flagtype.get(gov_name)
            if flag_type:
                variant_path = self.state.get_modded_path(os.path.join("gfx", "flags", f"{tag}_{flag_type}.tga"))
                flag = SafeLoader.safe_load_image(variant_path)
                if flag:
                    self.state.image_cache.set(cache_key, flag)
                    return flag
        
        base_path = self.state.get_modded_path(os.path.join("gfx", "flags", f"{tag}.tga"))
        flag = SafeLoader.safe_load_image(base_path)
        if flag:
            self.state.image_cache.set(cache_key, flag)
        return flag


class CountryFilter:
    """Renders the country filter list."""
    
    def __init__(self, parent_canvas, root, state, scrollbar_assets, image_refs, on_filter_change, gui, text_renderer):
        self.parent_canvas = parent_canvas
        self.root = root
        self.state = state
        self.sb_assets = scrollbar_assets
        self.image_refs = image_refs
        self.on_filter_change = on_filter_change
        self.gui = gui
        self.text_renderer = text_renderer  # ADD THIS
        self.font_manager = FontManager(self.state)
        self.selected_bg = self._load_selected_background()
        self.unselected_bg = self._load_unselected_background()
    
    def _load_selected_background(self) -> Optional[Image.Image]:
        """Load background for SELECTED countries."""
        btn_bg_path = self.state.get_modded_path(os.path.join("gfx", "interface", "diplo_countrybutton.dds"))
        full_img = SafeLoader.safe_load_image(btn_bg_path)
        if not full_img:
            return None
        
        crop_width = 620
        if full_img.width >= crop_width:
            row_bg = ImageLoader.crop(full_img, left=full_img.width - crop_width)
        else:
            row_bg = full_img
        
        return row_bg.resize((crop_width, 24), Image.Resampling.NEAREST)
    
    def _load_unselected_background(self) -> Optional[Image.Image]:
        """Load background for UNSELECTED countries."""
        btn_bg_path = self.state.get_modded_path(os.path.join("gfx", "interface", "diplo_countrybutton.dds"))
        full_img = SafeLoader.safe_load_image(btn_bg_path)
        if not full_img:
            return None
        
        crop_width = 620
        if full_img.width >= crop_width:
            row_bg = ImageLoader.crop(full_img, right=full_img.width - crop_width)
        else:
            row_bg = full_img
        
        return row_bg.resize((crop_width, 24), Image.Resampling.NEAREST)
    
    def create(self):
        """Create the country filter list."""
        scrollable = ScrollableList(self.parent_canvas, self.root, self.state, FILTER_LIST_X, FILTER_LIST_Y, 
                                   FILTER_LIST_WIDTH, FILTER_LIST_HEIGHT, self.sb_assets, row_height=24)
        scrollable.create()
        
        btn_bg_path = self.state.get_modded_path(os.path.join("gfx", "interface", "diplo_countrybutton.dds"))
        row_bg = self._load_row_background(btn_bg_path)
        
        overlay_path = self.state.get_modded_path(os.path.join("gfx", "interface", "flag_overlay_new.tga"))
        flag_overlay = SafeLoader.safe_load_image(overlay_path, size=(FLAG_WIDTH, FLAG_HEIGHT))
        
        all_tags = self._get_all_countries()
        
        for tag in all_tags:
            self._draw_country_row(scrollable.frame_inside, tag, row_bg, flag_overlay)
    
    def _load_row_background(self, path: str) -> Optional[Image.Image]:
        """Load and crop row background image."""
        full_img = SafeLoader.safe_load_image(path)
        if not full_img:
            return None
        
        crop_width = 620
        if full_img.width >= crop_width:
            row_bg = ImageLoader.crop(full_img, left=full_img.width - crop_width)
        else:
            row_bg = full_img
        
        return row_bg.resize((crop_width, 24), Image.Resampling.NEAREST)
    
    def _get_all_countries(self) -> List[str]:
        """Get all unique country tags from wars."""
        tags = set()
        for war in self.state.wars_data:
            tags.update(war.attackers)
            tags.update(war.defenders)
        return sorted([t for t in tags if t and t != "---"])
    
    def _draw_country_row(self, parent, tag: str, row_bg: Optional[Image.Image], flag_overlay: Optional[Image.Image]):
        """Draw a single country filter row with transparent background."""
        row = tk.Frame(parent, bg=BG_COLOR, height=24)
        row.pack(fill="x", pady=0)
        
        row_canvas = tk.Canvas(row, width=FILTER_LIST_WIDTH - SCROLLBAR_WIDTH - 4, height=24, 
                            highlightthickness=0, bg=BG_COLOR)
        row_canvas.pack(side="left", anchor="w")
        
        # Background - make transparent if we have row_bg
        if self.is_country_selected(tag):
            if self.selected_bg:
                # Convert to transparent
                bg_img = self.selected_bg.copy()
                if bg_img.mode != 'RGBA':
                    bg_img = bg_img.convert('RGBA')
                photo = ImageTk.PhotoImage(bg_img)
                self.image_refs.append(photo)
                row_canvas.create_image(0, 0, anchor="nw", image=photo)
        else:
            if self.unselected_bg:
                # Convert to transparent
                bg_img = self.unselected_bg.copy()
                if bg_img.mode != 'RGBA':
                    bg_img = bg_img.convert('RGBA')
                photo = ImageTk.PhotoImage(bg_img)
                self.image_refs.append(photo)
                row_canvas.create_image(0, 0, anchor="nw", image=photo)
        
        # Flag with overlay
        flag_x = 3
        flag_y = 3
        
        flag = self._load_flag(tag)
        if flag:
            flag = flag.resize((FLAG_WIDTH, FLAG_HEIGHT), Image.Resampling.LANCZOS)
            
            if flag_overlay:
                flag = flag.copy()
                flag.alpha_composite(flag_overlay)
            
            photo_flag = ImageTk.PhotoImage(flag)
            self.image_refs.append(photo_flag)
            row_canvas.create_image(flag_x, flag_y, anchor="nw", image=photo_flag)
        
        # Country name
        text_x = flag_x + FLAG_WIDTH + 3
        text_y = 12

        government = self.state.country_governments.get(tag)
        country_name = LocalizationParser.get_country_name(self.state, tag, government)

        if len(country_name) > 21:
            display_text = country_name[:19] + "..."
        else:
            display_text = country_name

        text_photo = self.text_renderer.render_cached_text(display_text, size=12, color="black", bold=True)
        row_canvas.create_image(text_x, text_y, anchor="w", image=text_photo)
        
        # Click handler
        def on_click(event):
            if tag in self.state.selected_countries:
                self.state.selected_countries.remove(tag)
            else:
                self.state.selected_countries.add(tag)
            
            # Update filtered wars and refresh display to update war count
            self.on_filter_change()  # This calls _rebuild_and_refresh()
            # The war count will update when Tab2 is redrawn
        
        row_canvas.bind("<Button-1>", on_click)
    
    def is_country_selected(self, tag: str) -> bool:
        """Check if a country is selected in the filter."""
        return tag in self.state.selected_countries
    
    def _load_flag(self, tag: str) -> Optional[Image.Image]:
        """Load flag image for a country tag."""
        if not tag or tag == "---":
            return None
        
        cache_key = f"flag_{tag}"
        cached_flag = self.state.image_cache.get(cache_key)
        if cached_flag:
            return cached_flag
        
        gov_name = self.state.country_governments.get(tag)
        if gov_name:
            flag_type = self.state.gov_to_flagtype.get(gov_name)
            if flag_type:
                variant_path = self.state.get_modded_path(os.path.join("gfx", "flags", f"{tag}_{flag_type}.tga"))
                flag = SafeLoader.safe_load_image(variant_path)
                if flag:
                    self.state.image_cache.set(cache_key, flag)
                    return flag
        
        base_path = self.state.get_modded_path(os.path.join("gfx", "flags", f"{tag}.tga"))
        flag = SafeLoader.safe_load_image(base_path)
        if flag:
            self.state.image_cache.set(cache_key, flag)
        return flag


# ============================================================================
# MAIN ENTRY POINT
# ============================================================================

def main():
    """Main entry point for the application."""
    app = tk.Tk()
    app.withdraw()
    
    messagebox.showinfo("Select Victoria 2 Folder", "Please select your Victoria 2 installation folder.")
    
    initial_path = filedialog.askdirectory(title="Select Victoria 2 Folder")
    if not initial_path:
        messagebox.showerror("Error", "No folder selected. Exiting.")
        app.destroy()
        return
    
    # Load configuration to get any saved mods
    config = AppConfig.load()
    
    # Create application state with updated parameter name
    state = AppState(vic2_path=initial_path, mod_names=config.default_mods.copy() if config.default_mods else [])
    
    gui = WarAnalyzerGUI(state)
    
    try:
        app.mainloop()
    except Exception as e:
        messagebox.showerror("Fatal Error", f"The application encountered an error:\n{e}")
    finally:
        # Force cleanup
        try:
            if 'gui' in locals():
                gui.cleanup()
        except:
            pass
        try:
            app.destroy()
        except:
            pass
        # Force exit to ensure process ends
        import os
        os._exit(0)


if __name__ == "__main__":
    main()
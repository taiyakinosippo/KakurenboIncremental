================================================================================
                          STYLIZED LIBRARY — ASSET PACK
================================================================================

Thank you for purchasing Stylized Library!
This asset pack contains a collection of 48 stylized, hand-crafted 3D props
designed for creating cozy library, study room, and adventure-themed
environments.


--------------------------------------------------------------------------------
  PACKAGE CONTENTS
--------------------------------------------------------------------------------

  48  Static meshes
  69  Textures (BaseColor, Normal, OcclusionRoughnessMetallic)
  23  Material Instances
   2  Master Materials (MM_Base_Color, MM_Floor)
   1  Demo map


--------------------------------------------------------------------------------
  INCLUDED MODELS
--------------------------------------------------------------------------------

  Furniture:        Armchair, Bookcase (x2), Shelf, Table, Pouf, Carpet,
                    Fireplace, Pillow

  Books & Scrolls:  Book (x8), Scroll (x7), Stack

  Adventure:        Globe, Gramophone, Telescope, Binocular, Compass,
                    Camera, Clock, Radio, Backpack, Suitcase (x2)

  Fishing:          FishBait, FishBoard

  Pottery:          Pot (x5)

  Miscellaneous:    Bucket, Scoop, Woodlog (x3)


--------------------------------------------------------------------------------
  REQUIREMENTS
--------------------------------------------------------------------------------

  - Unreal Engine 5.4 or newer
  - DirectX 12 capable GPU recommended


--------------------------------------------------------------------------------
  RENDERING FEATURES
--------------------------------------------------------------------------------

  The project is configured with the following settings enabled:

    - Lumen Global Illumination
    - Virtual Shadow Maps
    - Hardware Ray Tracing (optional, can be disabled)
    - Substrate material system
    - Shader Model 6 (DirectX 12)

  These settings provide the best visual quality but require a modern GPU.
  If you experience performance issues, Ray Tracing and Virtual Shadow Maps
  can be safely disabled in Project Settings without affecting the materials.


--------------------------------------------------------------------------------
  INSTALLATION
--------------------------------------------------------------------------------

  Option A — Open as a standalone project:
    1. Extract the Stylized_Library folder.
    2. Open the .uproject file with Unreal Engine.
    3. The demo map will load automatically.

  Option B — Migrate into your existing project:
    1. Open the Stylized_Library project in Unreal Engine.
    2. In Content Browser, right-click the Stylized_Library folder.
    3. Select Asset Actions > Migrate.
    4. Choose the Content folder of your target project.
    5. All assets, materials, and textures will be copied.


--------------------------------------------------------------------------------
  FOLDER STRUCTURE
--------------------------------------------------------------------------------

  Content/Stylized_Library/
  |
  |-- Maps/                  Demo map
  |
  |-- Materials/
  |   |-- Instances/         Material Instances (23)
  |   |-- Master/            Master Materials (2)
  |
  |-- Meshes/                Static meshes (48)
  |
  |-- Textures/              PBR texture sets organized by object
      |-- T_[ObjectName]/
          |-- *_BaseColor.uasset
          |-- *_Normal.uasset
          |-- *_OcclusionRoughnessMetallic.uasset


--------------------------------------------------------------------------------
  TEXTURES
--------------------------------------------------------------------------------

  Workflow:  PBR Metallic/Roughness

  Each texture set includes:
    - BaseColor                     Albedo / diffuse color
    - Normal                        Tangent-space normal map (DirectX format)
    - OcclusionRoughnessMetallic    Packed: R=AO, G=Roughness, B=Metallic


--------------------------------------------------------------------------------
  TEXTURE SETS & SHARED ATLASES
--------------------------------------------------------------------------------

  Some smaller props share a single texture atlas for better performance.

  T_Backpack ........ SM_Backpack
  T_Bookcase_01 ..... SM_Bookcase_01
  T_Bookcase_02 ..... SM_Bookcase_02
  T_Carpet .......... SM_Carpet
  T_Clock ........... SM_Clock
  T_Fireplace ....... SM_Fireplace
  T_FishBait ........ SM_FishBait
  T_FishBoard ....... SM_FishBoard
  T_Globe ........... SM_Globe
  T_Gramophone ...... SM_Gramophone
  T_Shelf ........... SM_Shelf
  T_Table ........... SM_Table
  T_Telescope ....... SM_Telescope
  T_PropsSetA ....... SM_Armchair, SM_Pouf, SM_Pillow
  T_PropsSetB ....... SM_Pot_01 — SM_Pot_05
  T_PropsSetC ....... SM_Binocular, SM_Radio
  T_PropsSetD ....... SM_Camera, SM_Compass
  T_PropsSetE ....... SM_Scroll_01 — SM_Scroll_07
  T_PropsSetF ....... SM_Bucket, SM_Scoop, SM_Stack
  T_PropsSetG ....... SM_Suitcase_01, SM_Suitcase_02
  T_PropsSetH ....... SM_Woodlog_01 — SM_Woodlog_03
  T_PropsSetO ....... SM_Book_01 — SM_Book_04
  T_PropsSetP ....... SM_Book_05 — SM_Book_08


--------------------------------------------------------------------------------
  NAMING CONVENTIONS
--------------------------------------------------------------------------------

  Meshes:              SM_[ObjectName]
  Textures:            T_[ObjectName]_[MapType]
  Material Instances:  MI_[ObjectName]
  Master Materials:    MM_[Name]


--------------------------------------------------------------------------------
  TIPS FOR BEST RESULTS
--------------------------------------------------------------------------------

  - All meshes use real-world scale (centimeters). No rescaling needed.
  - Check the demo map for reference on how to arrange the props.
  - Props sharing a texture atlas are designed to be used together
    in the same scene for optimal draw-call batching.
  - The Master Material (MM_Base_Color) is fully parameterized —
    all Material Instances derive from it for consistent look and easy editing.


--------------------------------------------------------------------------------
  SUPPORT
--------------------------------------------------------------------------------

  If you have any questions or issues, feel free to reach out
  through the Fab.com messaging system.


--------------------------------------------------------------------------------
  VERSION HISTORY
--------------------------------------------------------------------------------

  v1.0  -  Initial release


================================================================================

//
// pixdefs.h
//
// Defines values for the loader, etc. to know where to put
// things in the array.
//

#define PIX_H_WALL1     0
#define PIX_GRASS1      1
#define PIX_WATER1      2
#define PIX_VOID1       3
#define PIX_WALL2       4
#define PIX_WALL3       5
#define PIX_FLOOR1      6
#define PIX_WALL4       7
#define PIX_WALL5       8

#define PIX_CARPET_LL   9
#define PIX_CARPET_L    10
#define PIX_CARPET_B    11
#define PIX_CARPET_LR   12
#define PIX_CARPET_UR   13
#define PIX_CARPET_U    14
#define PIX_CARPET_UL   15
#define PIX_CARPET_M    33
#define PIX_CARPET_M2   34
#define PIX_CARPET_R    42
// The "small" carpet pieces
#define PIX_CARPET_SMALL_HOR   127
#define PIX_CARPET_SMALL_VER   128
#define PIX_CARPET_SMALL_CUP   129
#define PIX_CARPET_SMALL_CAP   130
#define PIX_CARPET_SMALL_LEFT  131
#define PIX_CARPET_SMALL_RIGHT 132
#define PIX_CARPET_SMALL_TINY  133


// More grass .. reorder when all use this
// defs file
#define PIX_GRASS2      16
#define PIX_GRASS3      17
#define PIX_GRASS4      18

#define PIX_GRASS_DARK_1  82
#define PIX_GRASS_DARK_2  86
#define PIX_GRASS_DARK_3  87
#define PIX_GRASS_DARK_4  88
#define PIX_GRASS_DARK_LL 83
#define PIX_GRASS_DARK_UR 84
#define PIX_GRASS_RUBBLE 85

#define PIX_GRASS_DARK_B1 92 // bottom 'fuzzy' edges
#define PIX_GRASS_DARK_B2 93
#define PIX_GRASS_DARK_BR 94 // bottom right fuzzy
#define PIX_GRASS_DARK_R1 95 // right fuzzy
#define PIX_GRASS_DARK_R2 96

#define PIX_GRASS_LIGHT_1   104 // lighter grass
#define PIX_GRASS_LIGHT_TOP 105
#define PIX_GRASS_LIGHT_RIGHT_TOP 106
#define PIX_GRASS_LIGHT_RIGHT 107
#define PIX_GRASS_LIGHT_RIGHT_BOTTOM 108
#define PIX_GRASS_LIGHT_BOTTOM 109
#define PIX_GRASS_LIGHT_LEFT_BOTTOM 110
#define PIX_GRASS_LIGHT_LEFT 111
#define PIX_GRASS_LIGHT_LEFT_TOP 112

#define PIX_WATER2      19
#define PIX_WATER3      20

#define PIX_PAVEMENT1   21
#define PIX_PAVEMENT2   51
#define PIX_PAVEMENT3   52

#define PIX_WALLSIDE1   22
#define PIX_WALLSIDE_L  23
#define PIX_WALLSIDE_R  24
#define PIX_WALLSIDE_C  25
#define PIX_WALLSIDE_CRACK_C1 102

#define PIX_WALL_LL     26

#define PIX_PAVESTEPS1  27

#define PIX_BRAZIER1    28

#define PIX_WATERGRASS_LL 29
#define PIX_WATERGRASS_LR 30
#define PIX_WATERGRASS_UL 31
#define PIX_WATERGRASS_UR 32
#define PIX_GRASSWATER_LL 47
#define PIX_GRASSWATER_LR 48
#define PIX_GRASSWATER_UL 49
#define PIX_GRASSWATER_UR 50

#define PIX_WATERGRASS_U 68
#define PIX_WATERGRASS_L 69
#define PIX_WATERGRASS_R 70
#define PIX_WATERGRASS_D 71

#define PIX_PAVESTEPS2  35
#define PIX_PAVESTEPS2L 36
#define PIX_PAVESTEPS2R 37

#define PIX_WALLTOP_H   38

#define PIX_TORCH1      39
#define PIX_TORCH2      40
#define PIX_TORCH3      41

#define PIX_FLOOR_PAVEL 43
#define PIX_FLOOR_PAVER 44
#define PIX_FLOOR_PAVEU 45
#define PIX_FLOOR_PAVED 46

#define PIX_COLUMN1     53
#define PIX_COLUMN2     54

// These are trees
#define PIX_TREE_B1     55

#define PIX_TREE_M1     56
#define PIX_TREE_ML     58 // left of center
#define PIX_TREE_T1     57
#define PIX_TREE_MR             80 // right of center
#define PIX_TREE_MT                             81  // thin

#define PIX_DIRT_1      59
#define PIX_DIRTGRASS_UL1 60
#define PIX_DIRTGRASS_UR1 61
#define PIX_DIRTGRASS_LL1 62
#define PIX_DIRTGRASS_LR1 63
#define PIX_DIRT_DARK_1 103
#define PIX_DIRTGRASS_DARK_UL1 98
#define PIX_DIRTGRASS_DARK_UR1 99
#define PIX_DIRTGRASS_DARK_LL1 100
#define PIX_DIRTGRASS_DARK_LR1 101

#define PIX_PATH_1      64
#define PIX_PATH_2      65
#define PIX_PATH_3      66
#define PIX_PATH_4      74

#define PIX_BOULDER_1   67
#define PIX_BOULDER_2   89
#define PIX_BOULDER_3   90
#define PIX_BOULDER_4   91

#define PIX_COBBLE_1    72
#define PIX_COBBLE_2    73
#define PIX_COBBLE_3    75
#define PIX_COBBLE_4    76

#define PIX_WALL_ARROW_GRASS 77
#define PIX_WALL_ARROW_FLOOR 78
#define PIX_WALL_ARROW_GRASS_DARK 97

// Cliff tiles
#define PIX_CLIFF_BOTTOM 113
#define PIX_CLIFF_TOP    114
#define PIX_CLIFF_LEFT   115
#define PIX_CLIFF_RIGHT  116
#define PIX_CLIFF_BACK_1 117
#define PIX_CLIFF_BACK_2 118
#define PIX_CLIFF_BACK_L 119
#define PIX_CLIFF_BACK_R 120
#define PIX_CLIFF_TOP_L  121
#define PIX_CLIFF_TOP_R  122

// Damaged tiles
#define PIX_GRASS1_DAMAGED 79

// Pete's graphics ..
#define PIX_JAGGED_GROUND_1 123
#define PIX_JAGGED_GROUND_2 124
#define PIX_JAGGED_GROUND_3 125
#define PIX_JAGGED_GROUND_4 126

// This should be the largest #defined pix +1
#define PIX_MAX 134  //last currently = PIX_CARPET_SMALL_TINY


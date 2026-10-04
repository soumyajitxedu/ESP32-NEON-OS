#ifndef CAT_PET_TYPES_H
#define CAT_PET_TYPES_H

#include <Arduino.h>

enum CatState
{
    CAT_IDLE,
    CAT_EATING,
    CAT_PLAYING,
    CAT_SLEEPING,
    CAT_SAD
};

enum CatBehavior
{
    B_IDLE,
    B_SLEEPING,
    B_NAPPING,
    B_STRETCHING,
    B_KNEADING,
    B_LOUNGING,
    B_INVESTIGATING,
    B_OBSERVING,
    B_CHATTERING,
    B_ZOOMIES,
    B_VOCALIZING,
    B_SELF_GROOMING,
    B_BEING_GROOMED,
    B_HUNTING,
    B_GIFT_BRINGING,
    B_PACING,
    B_SULKING,
    B_MISCHIEF,
    B_HIDING,
    B_TRAINING,
    B_PLAYING,
    B_AFFECTION,
    B_ATTENTION,
    B_EATING,
    B_STARTLED,
    B_MEANDERING
};

enum PotKind
{
    POT_SMALL,
    POT_MEDIUM,
    POT_LARGE,
    POT_PLANTER
};

enum SeedKind
{
    SEED_CAT_GRASS,
    SEED_FREESIA,
    SEED_SUNFLOWER,
    SEED_ROSE
};

enum PlantStage
{
    STAGE_EMPTY,
    STAGE_SEEDLING,
    STAGE_YOUNG,
    STAGE_GROWING,
    STAGE_MATURE,
    STAGE_THRIVING,
    STAGE_WILTING,
    STAGE_DEAD
};

struct Plant
{
    PotKind pot;
    SeedKind seed;
    PlantStage stage;
    uint8_t water;
    uint8_t growth;
    unsigned long lastUpdate;
};

struct Sprite
{
    uint8_t width;
    uint8_t height;
    const uint8_t *const *frames;
    const uint8_t *const *fill_frames;
    uint8_t frame_count;
    uint8_t extra_frames;
};
struct CharBody
{
    Sprite sprite;
    int8_t ax, ay, hx, hy, tx, ty;
    float speed;
};
struct CharHead
{
    Sprite sprite;
    int8_t ax, ay, ex, ey;
    float speed;
};
struct CharPart
{
    Sprite sprite;
    int8_t ax, ay;
    float speed;
};
struct Pose
{
    const CharBody *body;
    const CharHead *head;
    const CharPart *tail;
    const CharPart *eyes;
    bool hf, tl, hoff;
    int8_t hox, hoy;
};

#endif

namespace legit
{
  namespace Colors
  {
    //https://flatuicolors.com/palette/defo
#define RGBA_LE(col) (((col & 0xff000000) >> (3 * 8)) + ((col & 0x00ff0000) >> (1 * 8)) + ((col & 0x0000ff00) << (1 * 8)) + ((col & 0x000000ff) << (3 * 8)))
    uint32_t turqoise = RGBA_LE(0x1abc9cffu);
    uint32_t greenSea = RGBA_LE(0x16a085ffu);

    uint32_t emerald = RGBA_LE(0x2ecc71ffu);
    uint32_t nephritis = RGBA_LE(0x27ae60ffu);

    uint32_t peterRiver = RGBA_LE(0x3498dbffu); //blue
    uint32_t belizeHole = RGBA_LE(0x2980b9ffu);

    uint32_t amethyst = RGBA_LE(0x9b59b6ffu);
    uint32_t wisteria = RGBA_LE(0x8e44adffu);

    uint32_t sunFlower = RGBA_LE(0xf1c40fffu);
    uint32_t orange = RGBA_LE(0xf39c12ffu);

    uint32_t carrot = RGBA_LE(0xe67e22ffu);
    uint32_t pumpkin = RGBA_LE(0xd35400ffu);

    uint32_t alizarin = RGBA_LE(0xe74c3cffu);
    uint32_t pomegranate = RGBA_LE(0xc0392bffu);

    uint32_t clouds = RGBA_LE(0xecf0f1ffu);
    uint32_t silver = RGBA_LE(0xbdc3c7ffu);
    uint32_t imguiText = RGBA_LE(0xF2F5FAFFu);
  }
  struct ProfilerTask
  {
    double startTime;
    double endTime;
    std::string name;
    uint32_t color;
    double GetLength()
    {
      return endTime - startTime;
    }
  };
}
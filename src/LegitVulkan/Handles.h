namespace legit
{

  template<typename HandleInfo, typename Factory>
  struct UniqueHandle;

  template<typename HandleInfo, typename Factory>
  struct UniqueHandle
  {
    UniqueHandle()
    {
      isAttached = false;
    }
    UniqueHandle<HandleInfo, Factory> &operator = (UniqueHandle<HandleInfo, Factory> &other) = delete;
    UniqueHandle<HandleInfo, Factory> &operator = (UniqueHandle<HandleInfo, Factory> &&other)
    {
      if (isAttached)
      {
        this->info.Reset();
      }
      this->isAttached = other.isAttached;
      other.isAttached = false;
      this->info = other.info;
      return *this;
    }
    UniqueHandle(UniqueHandle<HandleInfo, Factory> &&other)
    {
      isAttached = other.isAttached;
      other.isAttached = false;
      this->info = other.info;
    }
    ~UniqueHandle()
    {
      if (isAttached)
        info.Reset();
    }
    void Detach()
    {
      isAttached = false;
    }
    void Reset()
    {
      Detach();
      info.Reset();
    }
    bool IsAttached()
    {
      return isAttached;
    }
    const HandleInfo &Get() const
    {
      return info;
    }
    const HandleInfo *operator ->() const
    {
      return &info;
    }
  private:
    UniqueHandle(const HandleInfo &info, bool isAttached = true):
      info(info),
      isAttached(isAttached)
    {
    }
    friend Factory;
    bool isAttached;
    HandleInfo info;
  };
}
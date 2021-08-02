namespace Utils
{
  template<typename T>
  struct Pool
  {
    struct Id
    {
      Id(size_t val) : asInt(val) {}
      Id() : asInt(size_t(-1)) {}
      bool operator == (const Id &other) const { return this->asInt == other.asInt; }
      size_t asInt;
    };
    struct Iterator
    {
      Iterator(Id id, Pool<T> &pool) :
        id(id),
        pool(pool)
      {
        Skip();
      }
      T &operator *()
      {
        return pool.Get(id);
      }
      void operator++()
      {
        id.asInt++;
        Skip();
      }
      bool operator != (const Iterator &other)
      {
        return this->id.asInt != other.id.asInt;
      }
    private:
      void Skip()
      {
        for (; id.asInt < pool.GetSize() && !pool.IsPresent(id); id.asInt++);
      }
      Id id;
      Pool<T> &pool;
    };

    Iterator begin()
    {
      return Iterator({ 0 }, *this);
    }
    Iterator end()
    {
      return Iterator({ GetSize() }, *this);
    }

    Id Add(T &&elem)
    {
      if (freeIds.size() > 0)
      {
        Id id = { freeIds.back() };
        freeIds.pop_back();
        data[id.asInt] = elem;
        isPresent[id.asInt] = true;
        return id;
      }
      else
      {
        Id id = { data.size() };
        data.push_back(elem);
        isPresent.push_back(true);
        return id;
      }
    }
    void Release(const Id &id)
    {
      assert(isPresent[id.asInt]);
      isPresent[id.asInt] = false;
      freeIds.push_back(id);
    }
    T& Get(const Id &id)
    {
      assert(id.asInt != size_t(-1));
      assert(id.asInt < data.size());
      assert(isPresent[id.asInt]);
      return data[id.asInt];
    }
    size_t GetSize()
    {
      return data.size();
    }
    bool IsPresent(Id id)
    {
      return isPresent[id.asInt];
    }
  private:
    std::vector<T> data;
    std::vector<bool> isPresent;
    std::vector<Id> freeIds;
  };
}
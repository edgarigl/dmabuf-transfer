struct mapped_range { 
    int memfd;
    void *addr;
    size_t length;
};

int create_range(struct mapped_range *range, const char *name,
                 size_t length, int id);
void destroy_range(struct mapped_range *range);

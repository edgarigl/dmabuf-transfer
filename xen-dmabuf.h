int xen_send_fd(uint32_t vmid, int sk_fd, int dma_fd);
int xen_receive_fd(uint32_t vmid, int fd);
void xen_imp_close(int fd);


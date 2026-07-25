# Opel MID Protocol Overview
Modified I2C but subverts, for example does not use read/write bits in the address.

## Sending Data Overview
1. Master pulls MRQ low for about 1ms
1. Slave pulls SDA low within about 500us
1. Master releases MRQ having seen SDA go low
1. Slave releases SDA released (high) shortly afterwards
1. After about 250us, master pulls SDA low
1. After about 250us, Master pulls SCL low
1. Master keeps MRQ high as it sends the address
    1. Send address as a data byte
    1. Note no send/receive bit
1. Master drops MRQ as it sends real data
1. Sending data byte
    1. SCL is low at this point.
    1. Master sets SDA high for 100us to indicate next byte start
    1. Master sets SDA high (1) or low (0) to indicate a data bit 1MSB first)
    1. Master clocks SCL high then low, for about 50us
    1. Repeats 8 times
    1. Master releases SDA to float high
    1. Slave pulls SDA low indicating ACK of data byte
        1. If SDA is still high, this is a NACK.
    1. Master pulls SCL high, reads SDA and reads the ACK/NACK
    1. Master pulls SCL low again
    1. Slave releases SDA which floats high again
1. Transmit next 8 bits
1. After final ACK, MRQ and SCL go high again then SDA goes high.
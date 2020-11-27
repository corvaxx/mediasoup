import { Channel } from './Channel';
import { EnhancedEventEmitter } from './EnhancedEventEmitter';
import { MediaKind } from './RtpParameters';
import { PayloadChannel } from './PayloadChannel';
import { Producer } from './Producer';
export declare class Mixer extends EnhancedEventEmitter {
    protected readonly _channel: Channel;
    protected readonly _payloadChannel: PayloadChannel;
    constructor({ channel, payloadChannel }: {
        channel: Channel;
        payloadChannel: PayloadChannel;
    });
    produce(kind: MediaKind): Promise<Producer>;
}
//# sourceMappingURL=Mixer.d.ts.map
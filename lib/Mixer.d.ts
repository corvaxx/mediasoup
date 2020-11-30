import { Channel } from './Channel';
import { EnhancedEventEmitter } from './EnhancedEventEmitter';
import { MediaKind } from './RtpParameters';
import { PayloadChannel } from './PayloadChannel';
import { Producer } from './Producer';
export declare class Mixer extends EnhancedEventEmitter {
    protected readonly _internal: {
        routerId: string;
        mixerId: string;
    };
    protected readonly _channel: Channel;
    protected readonly _payloadChannel: PayloadChannel;
    private readonly _appData?;
    protected _closed: boolean;
    protected readonly _producers: Map<string, Producer>;
    constructor({ internal, channel, payloadChannel, appData }: {
        internal: any;
        channel: Channel;
        payloadChannel: PayloadChannel;
        appData: any;
    });
    /**
     * Mixer id.
     */
    get id(): string;
    /**
     * Whether the Mixer is closed.
     */
    get closed(): boolean;
    /**
     * Close the Mixer.
     */
    close(): void;
    produce(kind: MediaKind): Promise<Producer>;
}
//# sourceMappingURL=Mixer.d.ts.map
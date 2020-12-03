import { Channel } from './Channel';
import { EnhancedEventEmitter } from './EnhancedEventEmitter';
import { MediaKind } from './RtpParameters';
import { PayloadChannel } from './PayloadChannel';
import { Producer } from './Producer';
import { RtpCapabilities } from './RtpParameters';
export declare enum MIXER_RENDER_MODE {
    SCALE = "scale",
    CROP = "crop",
    PAD = "pad"
}
export declare class Mixer extends EnhancedEventEmitter {
    protected readonly _internal: {
        routerId: string;
        mixerId: string;
    };
    protected readonly _channel: Channel;
    protected readonly _payloadChannel: PayloadChannel;
    private readonly _appData?;
    protected readonly _getRouterRtpCapabilities: () => RtpCapabilities;
    protected _closed: boolean;
    protected readonly _producers: Map<string, Producer>;
    protected readonly _rtpParameters: {
        codecs: ({
            clockRate: number;
            mimeType: string;
            parameters: {
                "level-asymmetry-allowed": number;
                "packetization-mode": number;
                "profile-level-id": string;
                apt?: undefined;
            };
            payloadType: number;
            rtcpFeedback: {
                parameter: string;
                type: string;
            }[];
        } | {
            clockRate: number;
            mimeType: string;
            parameters: {
                apt: number;
                "level-asymmetry-allowed"?: undefined;
                "packetization-mode"?: undefined;
                "profile-level-id"?: undefined;
            };
            payloadType: number;
            rtcpFeedback: never[];
        })[];
        encodings: {
            active: boolean;
            dtx: boolean;
            maxBitrate: number;
            rid: string;
            scalabilityMode: string;
            scaleResolutionDownBy: number;
        }[];
        headerExtensions: {
            encrypt: boolean;
            id: number;
            parameters: {};
            uri: string;
        }[];
        mid: string;
        rtcp: {
            cname: string;
            reducedSize: boolean;
        };
    };
    constructor({ internal, channel, payloadChannel, appData, getRouterRtpCapabilities }: {
        internal: any;
        channel: Channel;
        payloadChannel: PayloadChannel;
        appData: any;
        getRouterRtpCapabilities: () => RtpCapabilities;
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
    add(producer: Producer, kind: MediaKind, options: {
        x: number;
        y: number;
        width: number;
        height: number;
        z: number;
        mode: MIXER_RENDER_MODE;
    }): Promise<void>;
    update(producerId: string, options: {
        x: number;
        y: number;
        width: number;
        height: number;
        z: number;
        mode: MIXER_RENDER_MODE;
    }): Promise<void>;
    remove(producerId: string): Promise<void>;
}
//# sourceMappingURL=Mixer.d.ts.map
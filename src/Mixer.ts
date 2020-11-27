
import { Channel } from './Channel';
import { EnhancedEventEmitter } from './EnhancedEventEmitter';
import { Logger } from './Logger';
import { MediaKind } from './RtpParameters';
import { PayloadChannel } from './PayloadChannel';
import { Producer, ProducerOptions } from './Producer';

const logger = new Logger('Mixer');

export class Mixer extends EnhancedEventEmitter
{
    // Internal data.
    protected readonly _internal:
    {
        routerId: string;
        mixerId: string;
    };

    // Channel instance.
    protected readonly _channel: Channel;

    // PayloadChannel instance.
    protected readonly _payloadChannel: PayloadChannel;

    // Custom app data.
    private readonly _appData?: any;

    constructor({ internal, channel, payloadChannel, appData } : 
    { 
        internal       : any;
        channel        : Channel; 
        payloadChannel : PayloadChannel;
        appData        : any;
    })
    {
        super();

        logger.debug('constructor()');

        this._internal       = internal;
        this._channel        = channel;
        this._payloadChannel = payloadChannel;
        this._appData        = appData;
    }

    async produce(kind:MediaKind):Promise<Producer>
    {
        logger.debug('produce()');

        if (![ 'video' ].includes(kind))
            throw new TypeError(`invalid kind "${kind}"`);

        const internal = {};
        const reqData  = { kind };

        const status =
            await this._channel.request('mixer.produce', internal, reqData);

        const data    = {};
        const appData = {};


        const producer = new Producer(
            {
                internal,
                data,
                payloadChannel : this._payloadChannel,
                channel        : this._channel,
                appData,
                paused         : false
            });

        producer.on('@close', () =>
        {
        });

        return producer;
    }
}
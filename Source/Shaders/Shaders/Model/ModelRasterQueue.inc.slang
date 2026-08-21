#ifndef MODEL_RASTER_QUEUE_INCLUDED
#define MODEL_RASTER_QUEUE_INCLUDED

#define MODEL_RASTER_QUEUE_RESOLVE_CLASS_SHIFT 31u
#define MODEL_RASTER_QUEUE_RECORD_MASK 0x7FFFFFFFu

uint PackModelRasterQueueEntry(uint recordID, uint resolveClass)
{
    return recordID | ((resolveClass & 1u) << MODEL_RASTER_QUEUE_RESOLVE_CLASS_SHIFT);
}

uint ModelRasterQueueRecordID(uint entry)
{
    return entry & MODEL_RASTER_QUEUE_RECORD_MASK;
}

uint ModelRasterQueueResolveClass(uint entry)
{
    return entry >> MODEL_RASTER_QUEUE_RESOLVE_CLASS_SHIFT;
}

#endif

using UnityEngine;
using UnityEngine.Timeline;

namespace UTJ.Alembic
{
    [System.Serializable]
    [TrackClipType(typeof(AlembicRecorderClip))]
#if !UNITY_2018_2_OR_NEWER
    [TrackMediaType(TimelineAsset.MediaType.Script)]
#endif
    [TrackColor(0.33f, 0.0f, 0.08f)]
    public class AlembicRecorderTrack : TrackAsset
    {
    }
}

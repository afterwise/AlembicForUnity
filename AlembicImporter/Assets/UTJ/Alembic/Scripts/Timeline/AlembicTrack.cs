using UnityEngine;
using UnityEngine.Playables;
using UnityEngine.Timeline;

namespace UTJ.Alembic
{
    [System.Serializable]
    [TrackClipType(typeof(AlembicShotAsset))]
#if !UNITY_2018_2_OR_NEWER
    [TrackMediaType(TimelineAsset.MediaType.Script)]
#endif
    [TrackColor(0.53f, 0.0f, 0.08f)]
    public class AlembicTrack : TrackAsset
    {
        public override Playable CreateTrackMixer(PlayableGraph graph, GameObject gameObject, int inputCount)
        {
            return AlembicMixerPlayable.Create(graph, gameObject, inputCount, this);
        }
    }
}


using System;
using System.Linq;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.Playables;
using UnityEngine.Timeline;

namespace UTJ.Alembic
{
    public class AlembicMixerPlayable : PlayableBehaviour
    {
        public static ScriptPlayable<AlembicMixerPlayable> Create(
                PlayableGraph graph, GameObject gameObject, int inputCount, AlembicTrack track)
        {
            var playable = ScriptPlayable<AlembicMixerPlayable>.Create(graph, inputCount);
            playable.GetBehaviour().Initialize(gameObject, track);
            return playable;
        }

#if UNITY_EDITOR
        TimelineClip[] _clips;
        List<ValueTuple<AlembicStreamPlayer, double, int, double>> _clampedPlayers;
        PlayableDirector _director;
#endif

        void Initialize(GameObject gameObject, AlembicTrack track)
        {
#if UNITY_EDITOR
            Func<TrackAsset, Func<TimelineClip, bool>, TimelineClip[]> getClips = null;
            getClips = (track_, compare_) =>
                track_.GetChildTracks()
                .SelectMany(child => getClips(child, compare_))
                .Concat(track_.GetClips().Where(compare_))
                .OrderBy(clip => clip.start)
                .ToArray();

            _clips = getClips(track, clip => clip.asset is AlembicShotAsset).ToArray();
            _clampedPlayers = new List<ValueTuple<AlembicStreamPlayer, double, int, double>>();
            _director = gameObject.GetComponent<PlayableDirector>();
#endif
        }

#if UNITY_EDITOR
        void ClampPlayer(AlembicStreamPlayer player, double distance, int side, double clipIn)
        {
            for (int i = 0, n = _clampedPlayers.Count; i < n; ++i)
            {
                var x = _clampedPlayers[i];

                if (x.Item1 == player)
                {
                    if (x.Item2 > distance && x.Item3 != 0)
                        _clampedPlayers[i] = (player, distance, side, clipIn);

                    return;
                }
            }

            _clampedPlayers.Add((player, distance, side, clipIn));
        }
#endif

        public override void ProcessFrame(Playable playable, FrameData data, object userData)
        {
#if UNITY_EDITOR
            if (!Application.isPlaying && data.seekOccurred)
            {
                double time = playable.GetTime();

                foreach (var clip in _clips)
                {
                    var asset = (AlembicShotAsset) clip.asset;

                    if (asset == null)
                        continue;

                    var player = asset.streamPlayer.Resolve(_director);

                    if (player == null)
                        continue;

                    if (clip.start > time)
                        ClampPlayer(player, clip.start - time, -1, clip.clipIn);
                    else if (clip.end < time)
                        ClampPlayer(player, time - clip.end, 1, Math.Min(clip.clipIn + clip.duration, player.duration));
                    else
                        ClampPlayer(player, 0.0, 0, 0.0);
                }

                foreach (var x in _clampedPlayers)
                    if (x.Item3 != 0)
                        if (Math.Abs(x.Item1.currentTime - x.Item4) > 0.001)
                        {
                            x.Item1.currentTime = (float) x.Item4;
                            x.Item1.Update();
                        }

                _clampedPlayers.Clear();
            }
#endif
        }
    }
}


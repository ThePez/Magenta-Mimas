using UnityEngine;
using UnityEngine.Video;
using System.IO;
using UnityEngine.Experimental.Rendering;

public class VideoSpawner : MonoBehaviour
{
    [Header("Prefabs & Camera")] public GameObject spherePrefab;
    public GameObject screenPrefab;
    public Camera mainCamera;

    [Header("Layout")] public float padding = 1f; // space between spheres
    public float planeHeight = 2f; // height of the quad above each sphere

    void Start()
    {
        if (!mainCamera)
        {
            mainCamera = Camera.main;
        }

        if (!Directory.Exists(Application.streamingAssetsPath))
        {
            Debug.LogError("StreamingAssets folder does not exist!");
            return;
        }

        string[] mp4Files = Directory.GetFiles(Application.streamingAssetsPath, "*.mp4");

        int n = mp4Files.Length;
        if (n == 0)
        {
            Debug.LogError("No .mp4 files found.");
            return;
        }

        float radius = n * padding / (2f * Mathf.PI);
        for (int i = 0; i < n; i++)
        {
            float angle = Mathf.PI * (2f * i + 1) / n;
            Vector3 spherePos = new(Mathf.Cos(angle) * radius, 0f, Mathf.Sin(angle) * radius);

            GameObject sphere = Instantiate(spherePrefab, transform);
            sphere.transform.localPosition = spherePos;

            GameObject screen = Instantiate(screenPrefab, sphere.transform);
            screen.transform.localPosition = Vector3.up * planeHeight;
            screen.transform.LookAt(mainCamera?.transform);

            AddVideoPlayerToScreen(screen, mp4Files[i]);
        }
    }

    private void AddVideoPlayerToScreen(GameObject screen, string filename)
    {
        RenderTexture texture = new RenderTexture(1920, 1080, 0)
        {
            antiAliasing = 1,
            graphicsFormat = GraphicsFormat.R8G8B8A8_UNorm,
            depthStencilFormat = GraphicsFormat.None,
            useMipMap = false,
            wrapMode = TextureWrapMode.Clamp,
            filterMode = FilterMode.Point,
            anisoLevel = 0
        };

        screen.GetComponent<Renderer>().material = new Material(Shader.Find("HDRP/Unlit"))
        {
            mainTexture = texture
        };

        texture.Create();

        VideoPlayer videoPlayer = screen.AddComponent<VideoPlayer>();
        videoPlayer.playOnAwake = true;
        videoPlayer.waitForFirstFrame = true;
        videoPlayer.isLooping = true;
        videoPlayer.skipOnDrop = false;

        videoPlayer.renderMode = VideoRenderMode.RenderTexture;
        videoPlayer.targetTexture = texture;

        videoPlayer.source = VideoSource.Url;
        videoPlayer.url = Path.Combine(Application.streamingAssetsPath, Path.GetFileName(filename));

        videoPlayer.audioOutputMode = VideoAudioOutputMode.Direct;
        videoPlayer.timeUpdateMode = VideoTimeUpdateMode.DSPTime;
        videoPlayer.sendFrameReadyEvents = true;

        videoPlayer.frameReady += (source, _) =>
        {
            source.Pause();
            source.sendFrameReadyEvents = false;
        };

        screen.AddComponent<ResetVideo>();
        videoPlayer.Prepare();
    }
}
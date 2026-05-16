using UnityEngine;
using UnityEngine.Video;
using System.IO;

public class VideoSphereSpawnerFixedScale : MonoBehaviour
{
    [Header("Prefabs & Camera")] public GameObject spherePrefab;
    public Camera mainCamera;

    [Header("Layout")] public float padding = 1f; // space between spheres
    public float planeHeight = 2f; // height of the quad above each sphere
    public float fixedPlaneHeight = 1.5f; // world‑units for the quad’s height

    private GameObject[] spheres;
    private GameObject[] videoPlanes;

    void Start()
    {
        if (mainCamera)
        {
            mainCamera = Camera.main;
        }

        string path = Application.streamingAssetsPath;
        if (!Directory.Exists(path))
        {
            Debug.LogError("StreamingAssets folder does not exist!");
            return;
        }

        string[] mp4Files = Directory.GetFiles(path, "*.mp4");

        int n = mp4Files.Length;
        if (n == 0)
        {
            Debug.LogError("No .mp4 files found.");
            return;
        }

        spheres = new GameObject[n];
        videoPlanes = new GameObject[n];

        float radius = n * padding / (2f * Mathf.PI);

        for (int i = 0; i < n; i++)
        {
            float angle = Mathf.PI * (2f * i + 1) / n;
            Vector3 spherePos = new(Mathf.Cos(angle) * radius, 0f, Mathf.Sin(angle) * radius);
            spheres[i] =
                Instantiate(spherePrefab, transform.position + spherePos, Quaternion.identity, transform);

            videoPlanes[i] = CreateVideoQuad(spheres[i], mp4Files[i]);
            videoPlanes[i].transform.LookAt(mainCamera?.transform);
            videoPlanes[i].transform.Rotate(0f, 180f, 0f);
        }
    }
    
    // -----------------------------------------------------------------
    // Helper: creates the quad, video player, render texture, etc.
    // -----------------------------------------------------------------
    GameObject CreateVideoQuad(GameObject parentSphere, string videoPath)
    {
        GameObject plane = GameObject.CreatePrimitive(PrimitiveType.Quad);
        plane.transform.parent = parentSphere.transform;
        plane.transform.localPosition = Vector3.up * planeHeight;
        plane.AddComponent<ResetVideo>();

        // colliders & rigidbody
        Destroy(plane.GetComponent<Collider>());
        BoxCollider box = plane.AddComponent<BoxCollider>();
        box.isTrigger = true;

        // RenderTexture & material
        RenderTexture rt = new RenderTexture(1920, 1080, 0);
        rt.Create();

        Rigidbody rb = plane.AddComponent<Rigidbody>();
        rb.isKinematic = true;

        // VideoPlayer
        VideoPlayer vp = plane.AddComponent<VideoPlayer>();
        vp.playOnAwake = false;
        vp.source = VideoSource.Url;
        vp.isLooping = true;
        vp.waitForFirstFrame = true;
        vp.url = Path.Combine(Application.streamingAssetsPath, Path.GetFileName(videoPath));
        vp.targetTexture = rt;

        // Audio
        AudioSource audio = plane.AddComponent<AudioSource>();
        vp.audioOutputMode = VideoAudioOutputMode.AudioSource;
        vp.SetTargetAudioSource(0, audio);

        Material videoMat = new Material(Shader.Find("Unlit/Texture"));
        videoMat.mainTexture = rt;

        plane.GetComponent<Renderer>().material = videoMat;

        // Adjust size when video is prepared
        vp.prepareCompleted += source =>
        {
            uint texW = source.width;
            uint texH = source.height;

            plane.transform.localScale = new Vector3(fixedPlaneHeight * ((float)texW / texH), fixedPlaneHeight, 1f);

            RenderTexture rtAdj = new RenderTexture((int)texW, (int)texH, 0);
            rtAdj.Create();
            videoMat.mainTexture = rtAdj;
            source.targetTexture = rtAdj;

            source.frameReady += (s, frameIdx) =>
            {
                plane.GetComponent<Renderer>().material.mainTexture = s.targetTexture;
                s.Pause(); // keep paused after first frame
            };
            source.Pause();
        };

        vp.Prepare(); // async preparation, does not start playback
        return plane;
    }
}
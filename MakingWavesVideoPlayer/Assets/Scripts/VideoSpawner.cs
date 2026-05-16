using UnityEngine;
using UnityEngine.Video;
using System.IO;

public class VideoSpawner : MonoBehaviour
{
    [Header("Prefabs & Camera")] 
    public GameObject spherePrefab;

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
            
            Instantiate(spherePrefab, transform.position + spherePos, Quaternion.identity, transform);
            GameObject screen = Instantiate(screenPrefab, 
                transform.position + spherePos + Vector3.up * planeHeight, Quaternion.identity, transform);
            
            screen.transform.LookAt(mainCamera?.transform);
            screen.transform.Rotate(0f, 180f, 0f);

            // VideoPlayer
            VideoPlayer videoPlayer = screen.GetComponent<VideoPlayer>();
            videoPlayer.url = Path.Combine(Application.streamingAssetsPath, Path.GetFileName(mp4Files[i]));
            videoPlayer.Prepare();
        }
    }
}